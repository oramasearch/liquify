// SPDX-License-Identifier: MIT
//
// AddressSanitizer harness for tree-sitter and the pinned C and C++ grammars (liquify#92).
//
// Every input is parsed with both grammars: each example of the upstream test corpora, each
// highlight test and example file, every prefix and suffix of those inputs (sampled for large
// files), seeded mutations of them reparsed both from scratch and incrementally after
// `ts_tree_edit`, the same bytes read as UTF-16, and a generated set of raw string literals aimed
// at the C++ grammar's external scanner. Every tree is walked node by node.
//
// The harness itself reports nothing about tree shapes: a parse error is the expected outcome for
// malformed input. Its result is the AddressSanitizer log. Build with -fsanitize-recover=address
// and run with ASAN_OPTIONS=halt_on_error=0 so every report is collected rather than the first;
// each report is followed by the input that produced it, and that input is written to the
// reproduction directory.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sanitizer/asan_interface.h>
#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_c();
extern "C" const TSLanguage* tree_sitter_cpp();

namespace {

namespace fs = std::filesystem;

enum class Kind { corpus, source, generated };

struct Input {
    std::string label;
    std::string text;
    Kind kind = Kind::source;
};

struct Language {
    std::string_view name;
    const TSLanguage* language;
};

struct Stats {
    std::size_t files = 0;
    std::size_t corpus_examples = 0;
    std::size_t source_files = 0;
    std::size_t generated = 0;
    std::size_t full_parses = 0;
    std::size_t slice_parses = 0;
    std::size_t mutation_parses = 0;
    std::size_t incremental_parses = 0;
    std::size_t utf16_parses = 0;
    std::size_t nodes = 0;
    std::size_t bytes = 0;
    // Unmodified inputs that parsed with an error node, by kind of input and grammar (c, cpp).
    std::size_t with_errors[3][2] = {};
    std::size_t originals[3][2] = {};
};

Stats stats;

// What is being parsed, read by the report callback.
struct Current {
    std::string_view language;
    std::string_view operation;
    const std::string* label = nullptr;
    const std::string* text = nullptr;
};

Current current;
std::size_t reports = 0;
fs::path repro_dir;

extern "C" void on_asan_report(const char* /*report*/) {
    ++reports;
    std::fprintf(stderr, "\n=== liquify tree-sitter-asan: report %zu while parsing\n", reports);
    std::fprintf(stderr, "===   language:  %.*s\n", static_cast<int>(current.language.size()),
                 current.language.data());
    std::fprintf(stderr, "===   operation: %.*s\n", static_cast<int>(current.operation.size()),
                 current.operation.data());
    std::fprintf(stderr, "===   input:     %s\n", current.label ? current.label->c_str() : "?");
    if (current.text && !repro_dir.empty()) {
        auto path = repro_dir / ("report-" + std::to_string(reports) + ".bin");
        auto out = std::ofstream(path, std::ios::binary);
        out.write(current.text->data(), static_cast<std::streamsize>(current.text->size()));
        std::fprintf(stderr, "===   saved to:  %s\n", path.c_str());
    }
}

auto read_file(const fs::path& path) -> std::string {
    auto in = std::ifstream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

auto line_without_ending(std::string_view line) -> std::string_view {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    return line;
}

auto starts_with_run(std::string_view line, char c) -> bool {
    return line.size() >= 3 && line[0] == c && line[1] == c && line[2] == c;
}

// The source of each example in a tree-sitter corpus file. An example is a header (a line of
// '=', name lines, a line of '='), its source, a line of '-', and the expected tree.
auto corpus_examples(const std::string& file, const std::string& label) -> std::vector<Input> {
    enum class State { outside, header, source, expected };
    auto examples = std::vector<Input>{};
    auto state = State::outside;
    auto name = std::string{};
    auto source = std::string{};
    auto at = std::size_t{0};
    while (at < file.size()) {
        auto end = file.find('\n', at);
        end = end == std::string::npos ? file.size() : end + 1;
        const auto line = std::string_view(file).substr(at, end - at);
        const auto bare = line_without_ending(line);
        at = end;
        switch (state) {
        case State::outside:
        case State::expected:
            if (starts_with_run(bare, '=')) {
                state = State::header;
                name.clear();
            }
            break;
        case State::header:
            if (starts_with_run(bare, '=')) {
                state = State::source;
                source.clear();
            } else {
                name += name.empty() ? "" : " ";
                name += bare;
            }
            break;
        case State::source:
            if (starts_with_run(bare, '-')) {
                // The line ending before the divider belongs to the divider.
                if (source.ends_with("\r\n")) {
                    source.resize(source.size() - 2);
                } else if (source.ends_with('\n')) {
                    source.pop_back();
                }
                examples.push_back({label + " :: " + name, source, Kind::corpus});
                state = State::expected;
            } else {
                source += line;
            }
            break;
        }
    }
    return examples;
}

class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    auto next() -> std::uint64_t {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_;
    }
    auto below(std::size_t bound) -> std::size_t {
        return bound == 0 ? 0 : static_cast<std::size_t>(next() % bound);
    }

private:
    std::uint64_t state_;
};

auto fnv1a(std::string_view text) -> std::uint64_t {
    auto hash = std::uint64_t{0xcbf29ce484222325ULL};
    for (const auto c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Fragments that stress the lexers, the C++ raw string scanner and error recovery.
auto dictionary() -> const std::vector<std::string>& {
    static const auto words = std::vector<std::string>{
        "R\"", "R\"(", ")\"", "R\"abc(", ")abc\"", ")ab\"", "R\"0123456789abcdef(",
        ")0123456789abcdef\"", "R\"0123456789abcdefg(", "u8R\"x(", "LR\"(", "uR\"d(", "UR\"",
        "R\"a b(", "R\"a\\b(", "R\"\n(", "R\"\xf0\x9f\x98\x80(", ")\xf0\x9f\x98\x80\"", "\"", "'",
        "\\", "\\\n", "\\\r\n", "\r\n", "\r", std::string(1, '\0'), "\xff", "\xfe\xff", "\xc3",
        "\xe2\x82", "\xf0\x9f\x98", "\xed\xa0\x80", "/*", "*/", "//", "#", "#if X\n", "#endif\n",
        "#define X(a, b) a##b\n", "#include <", "<", ">", ">>", "template <", "::", "{", "}", "(",
        ")", "[[", "]]", ";", ",", "__declspec(", "operator\"\"_x", "co_await ", "requires ",
        "concept C = ", "0x", "1e+", "'\\u", "L'", "u8\"", "...", "->*", "<=>", "decltype(auto)",
        "typename ", "extern \"C\" {", "struct S : ", "enum class E : int {", "asm(\"",
    };
    return words;
}

auto point_at(std::string_view text, std::size_t byte) -> TSPoint {
    auto row = std::uint32_t{0};
    auto column = std::uint32_t{0};
    for (auto i = std::size_t{0}; i < byte && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++row;
            column = 0;
        } else {
            ++column;
        }
    }
    return {row, column};
}

struct Mutation {
    std::string text;
    std::size_t start;
    std::size_t old_end;
    std::size_t new_end;
};

// One seeded change to `text`, described as the edit tree-sitter is told about.
auto mutate(const std::string& text, Rng& rng) -> Mutation {
    const auto& words = dictionary();
    const auto size = text.size();
    const auto pos = rng.below(size + 1);
    switch (rng.below(6)) {
    case 0: { // overwrite one byte
        if (size == 0) {
            break;
        }
        const auto at = rng.below(size);
        auto changed = text;
        changed[at] = static_cast<char>(rng.below(256));
        return {changed, at, at + 1, at + 1};
    }
    case 1: { // insert a dictionary fragment
        const auto& word = words[rng.below(words.size())];
        return {text.substr(0, pos) + word + text.substr(pos), pos, pos, pos + word.size()};
    }
    case 2: { // delete a range
        const auto length = std::min(size - pos, rng.below(17));
        return {text.substr(0, pos) + text.substr(pos + length), pos, pos + length, pos};
    }
    case 3: { // duplicate a range
        const auto length = std::min(size - pos, rng.below(65));
        const auto copy = text.substr(pos, length);
        return {text.substr(0, pos) + copy + text.substr(pos), pos, pos, pos + length};
    }
    case 4: { // insert arbitrary bytes
        auto bytes = std::string{};
        for (auto n = rng.below(8) + 1; n > 0; --n) {
            bytes += static_cast<char>(rng.below(256));
        }
        return {text.substr(0, pos) + bytes + text.substr(pos), pos, pos, pos + bytes.size()};
    }
    default: { // cut the rest and end in a fragment
        const auto& word = words[rng.below(words.size())];
        return {text.substr(0, pos) + word, pos, size, pos + word.size()};
    }
    }
    const auto& word = words[rng.below(words.size())];
    return {text + word, size, size, size + word.size()};
}

// Raw string literals around the C++ scanner's delimiter limit, unterminated, mismatched and
// nested, with every encoding prefix.
auto raw_string_inputs() -> std::vector<Input> {
    auto inputs = std::vector<Input>{};
    const auto prefixes = std::vector<std::string>{"R", "u8R", "uR", "UR", "LR"};
    const auto alphabet = std::string("ab0)\"x\xc3\xa9\xf0\x9f\x98\x80");
    for (const auto& prefix : prefixes) {
        for (auto length = std::size_t{0}; length <= 20; ++length) {
            auto delimiter = std::string{};
            for (auto i = std::size_t{0}; i < length; ++i) {
                delimiter += static_cast<char>('a' + (i % 26));
            }
            const auto open = prefix + "\"" + delimiter + "(";
            const auto forms = std::vector<std::string>{
                open + "content)" + delimiter + "\"",
                open + "content",
                open + "content)" + delimiter,
                open + "content)" + delimiter.substr(0, length / 2) + "\"",
                open + ")" + delimiter + ")" + delimiter + "\")" + delimiter + "\"",
                open + alphabet + ")" + delimiter + "\"" + alphabet,
                "auto s = " + open + "\n\n)" + delimiter + "\";\nint x = " + open,
                prefix + "\"" + delimiter + " (x)" + delimiter + "\"",
                prefix + "\"" + delimiter + "\\(x)" + delimiter + "\"",
                prefix + "\"" + delimiter + "\n(x)" + delimiter + "\"",
                "#define M " + open + "x)" + delimiter + "\"\nM",
            };
            for (auto i = std::size_t{0}; i < forms.size(); ++i) {
                inputs.push_back({"generated raw string " + prefix + " delimiter " +
                                      std::to_string(length) + " form " + std::to_string(i),
                                  forms[i], Kind::generated});
            }
        }
    }
    return inputs;
}

auto walk(TSTree* tree) -> void {
    auto root = ts_tree_root_node(tree);
    auto cursor = ts_tree_cursor_new(root);
    for (;;) {
        const auto node = ts_tree_cursor_current_node(&cursor);
        ++stats.nodes;
        static_cast<void>(ts_node_type(node));
        static_cast<void>(ts_node_grammar_type(node));
        static_cast<void>(ts_node_start_byte(node));
        static_cast<void>(ts_node_end_point(node));
        static_cast<void>(ts_node_is_error(node));
        static_cast<void>(ts_node_is_missing(node));
        static_cast<void>(ts_tree_cursor_current_field_name(&cursor));
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            continue;
        }
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                ts_tree_cursor_delete(&cursor);
                return;
            }
        }
    }
}

auto parse(TSParser* parser, TSTree* old_tree, const std::string& text,
           TSInputEncoding encoding = TSInputEncodingUTF8) -> TSTree* {
    stats.bytes += text.size();
    auto* tree = ts_parser_parse_string_encoding(parser, old_tree, text.data(),
                                                 static_cast<std::uint32_t>(text.size()), encoding);
    if (tree) {
        walk(tree);
    }
    return tree;
}

struct Options {
    std::size_t mutations = 32;
    std::size_t max_slices = 512;
    std::uint64_t seed = 92;
};

auto run_input(const Input& input, std::size_t grammar, const Language& language,
               TSParser* parser, const Options& options) -> void {
    current = {.language = language.name, .label = &input.label, .text = &input.text};
    ts_parser_set_language(parser, language.language);

    current.operation = "full parse";
    auto* original = parse(parser, nullptr, input.text);
    ++stats.full_parses;
    const auto kind = static_cast<std::size_t>(input.kind);
    ++stats.originals[kind][grammar];
    if (original && ts_node_has_error(ts_tree_root_node(original))) {
        ++stats.with_errors[kind][grammar];
    }
    if (original && input.text.size() <= 64 * 1024) {
        std::free(ts_node_string(ts_tree_root_node(original)));
    }

    // Prefixes and suffixes: every cut point for small inputs, an even sample for large ones.
    const auto size = input.text.size();
    const auto step = std::max<std::size_t>(1, size / options.max_slices);
    for (auto cut = std::size_t{0}; cut <= size; cut += step) {
        const auto prefix = input.text.substr(0, cut);
        const auto suffix = input.text.substr(cut);
        current.text = &prefix;
        current.operation = "prefix";
        ts_tree_delete(parse(parser, nullptr, prefix));
        current.text = &suffix;
        current.operation = "suffix";
        ts_tree_delete(parse(parser, nullptr, suffix));
        stats.slice_parses += 2;
    }

    // Seeded mutations, parsed from scratch and reparsed from the edited original tree.
    auto rng = Rng(options.seed ^ fnv1a(input.label) ^ fnv1a(language.name));
    for (auto i = std::size_t{0}; i < options.mutations; ++i) {
        const auto mutation = mutate(input.text, rng);
        current.text = &mutation.text;
        current.operation = "mutation, fresh parse";
        ts_tree_delete(parse(parser, nullptr, mutation.text));
        ++stats.mutation_parses;
        if (!original) {
            continue;
        }
        current.operation = "mutation, incremental reparse";
        auto* edited = ts_tree_copy(original);
        const auto edit = TSInputEdit{
            .start_byte = static_cast<std::uint32_t>(mutation.start),
            .old_end_byte = static_cast<std::uint32_t>(mutation.old_end),
            .new_end_byte = static_cast<std::uint32_t>(mutation.new_end),
            .start_point = point_at(input.text, mutation.start),
            .old_end_point = point_at(input.text, mutation.old_end),
            .new_end_point = point_at(mutation.text, mutation.new_end),
        };
        ts_tree_edit(edited, &edit);
        auto* reparsed = parse(parser, edited, mutation.text);
        ++stats.incremental_parses;
        if (reparsed) {
            auto count = std::uint32_t{0};
            std::free(ts_tree_get_changed_ranges(edited, reparsed, &count));
        }
        ts_tree_delete(reparsed);
        ts_tree_delete(edited);
    }

    // The same bytes read as UTF-16, including odd lengths and unpaired surrogates.
    current.text = &input.text;
    current.operation = "UTF-16LE parse";
    ts_tree_delete(parse(parser, nullptr, input.text, TSInputEncodingUTF16LE));
    current.operation = "UTF-16BE parse";
    ts_tree_delete(parse(parser, nullptr, input.text, TSInputEncodingUTF16BE));
    stats.utf16_parses += 2;

    ts_tree_delete(original);
}

auto collect(const fs::path& path, std::vector<Input>& inputs) -> void {
    auto files = std::vector<fs::path>{};
    if (fs::is_directory(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path());
            }
        }
    } else {
        files.push_back(path);
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        ++stats.files;
        const auto text = read_file(file);
        const auto label = file.string();
        // A file under a directory named "corpus" is in tree-sitter's corpus format.
        auto in_corpus = false;
        for (const auto& part : file) {
            in_corpus = in_corpus || part == "corpus";
        }
        if (in_corpus) {
            auto examples = corpus_examples(text, label);
            stats.corpus_examples += examples.size();
            for (auto& example : examples) {
                inputs.push_back(std::move(example));
            }
        } else {
            ++stats.source_files;
            inputs.push_back({label, text});
        }
    }
}

auto usage() -> int {
    std::fprintf(stderr,
                 "usage: tree-sitter-asan [--mutations N] [--max-slices N] [--seed S] "
                 "[--repro-dir DIR] PATH...\n"
                 "       tree-sitter-asan --self-test\n"
                 "  PATH is a file or directory; files under a directory named corpus are read\n"
                 "  as tree-sitter corpus files, every other file as one source input.\n");
    return 2;
}

} // namespace

auto main(int argc, char** argv) -> int {
    auto options = Options{};
    auto paths = std::vector<fs::path>{};
    for (auto i = 1; i < argc; ++i) {
        const auto arg = std::string_view(argv[i]);
        const auto has_value = i + 1 < argc;
        if (arg == "--mutations" && has_value) {
            options.mutations = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--max-slices" && has_value) {
            options.max_slices = std::max<std::size_t>(1, std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--seed" && has_value) {
            options.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--repro-dir" && has_value) {
            repro_dir = argv[++i];
            fs::create_directories(repro_dir);
        } else if (arg == "--self-test") {
            // A deliberate heap overflow: proves the build is instrumented and a report is caught.
            __asan_set_error_report_callback(on_asan_report);
            auto* bytes = static_cast<volatile char*>(std::malloc(8));
            bytes[8] = 1;
            std::free(const_cast<char*>(bytes));
            std::printf("tree-sitter-asan: self-test reports: %zu (expected 1)\n", reports);
            return reports == 1 ? 0 : 1;
        } else if (arg.starts_with("--")) {
            return usage();
        } else {
            paths.emplace_back(arg);
        }
    }
    if (paths.empty()) {
        return usage();
    }
    __asan_set_error_report_callback(on_asan_report);

    auto inputs = std::vector<Input>{};
    for (const auto& path : paths) {
        if (!fs::exists(path)) {
            std::fprintf(stderr, "tree-sitter-asan: no such path: %s\n", path.c_str());
            return 2;
        }
        collect(path, inputs);
    }
    auto generated = raw_string_inputs();
    stats.generated = generated.size();
    for (auto& input : generated) {
        inputs.push_back(std::move(input));
    }

    const auto languages = std::vector<Language>{{"c", tree_sitter_c()}, {"cpp", tree_sitter_cpp()}};
    auto* parser = ts_parser_new();
    for (const auto& input : inputs) {
        for (auto grammar = std::size_t{0}; grammar < languages.size(); ++grammar) {
            run_input(input, grammar, languages[grammar], parser, options);
        }
    }
    ts_parser_delete(parser);

    std::printf("tree-sitter-asan: %zu files (%zu corpus examples, %zu source files), %zu generated "
                "raw string inputs; %zu inputs x %zu grammars\n",
                stats.files, stats.corpus_examples, stats.source_files, stats.generated,
                inputs.size(), languages.size());
    std::printf("tree-sitter-asan: parses: %zu full, %zu prefix/suffix, %zu mutated, %zu incremental, "
                "%zu UTF-16; %zu nodes walked, %zu bytes parsed\n",
                stats.full_parses, stats.slice_parses, stats.mutation_parses,
                stats.incremental_parses, stats.utf16_parses, stats.nodes, stats.bytes);
    // A parse error on an unmodified input is not a finding: corpus files include error-recovery
    // tests, each grammar also reads the other's corpus, and most generated inputs are malformed.
    // The counts show the inputs reached the grammars as intended.
    constexpr const char* kind_names[] = {"corpus examples", "source files", "generated inputs"};
    for (auto kind = 0; kind < 3; ++kind) {
        std::printf("tree-sitter-asan: %s with an error node: c %zu of %zu, cpp %zu of %zu\n",
                    kind_names[kind], stats.with_errors[kind][0], stats.originals[kind][0],
                    stats.with_errors[kind][1], stats.originals[kind][1]);
    }
    std::printf("tree-sitter-asan: AddressSanitizer reports: %zu\n", reports);
    return reports == 0 ? 0 : 1;
}
