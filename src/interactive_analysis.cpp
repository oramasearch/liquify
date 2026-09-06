#include "liquify/interactive_analysis.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "liquid/runtime.hpp"

extern "C" {
const TSLanguage* tree_sitter_c();
const TSLanguage* tree_sitter_cpp();
}

namespace liquify {
namespace {

constexpr auto no_parent = std::numeric_limits<std::size_t>::max();
constexpr auto open_buffers_input_name = "liquify.open-buffers";
constexpr auto analysis_index_input_name = "liquify.analysis-index";
constexpr auto translation_units_input_name = "liquify.translation-units";

auto failure(std::string message, std::string context = {}) -> liquid::error {
    return {liquid::errc::invalid_argument, std::move(message), std::move(context)};
}

auto bytes(std::string_view value) -> std::vector<std::byte> {
    auto output = std::vector<std::byte>(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        output[index] = static_cast<std::byte>(value[index]);
    }
    return output;
}

auto text(std::span<const std::byte> value) -> std::string {
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

auto path_key(const std::filesystem::path& path) -> std::string {
    return path.lexically_normal().generic_string();
}

class Encoder {
public:
    void u8(std::uint8_t value) {
        output_.push_back(static_cast<std::byte>(value));
    }

    void u32(std::uint32_t value) {
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            output_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    void u64(std::uint64_t value) {
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            output_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    void string(std::string_view value) {
        u32(static_cast<std::uint32_t>(value.size()));
        const auto encoded = bytes(value);
        output_.insert(output_.end(), encoded.begin(), encoded.end());
    }

    void range(const SourceRange& value) {
        u32(value.start_byte);
        u32(value.end_byte);
        u32(value.start_line);
        u32(value.start_column);
        u32(value.end_line);
        u32(value.end_column);
    }

    [[nodiscard]] auto take() && -> std::vector<std::byte> {
        return std::move(output_);
    }

private:
    std::vector<std::byte> output_;
};

class Decoder {
public:
    explicit Decoder(std::span<const std::byte> input) : input_(input) {}

    [[nodiscard]] auto u8() -> liquid::result<std::uint8_t> {
        if (cursor_ == input_.size()) {
            return std::unexpected(failure("truncated interactive value"));
        }
        return static_cast<std::uint8_t>(input_[cursor_++]);
    }

    [[nodiscard]] auto u32() -> liquid::result<std::uint32_t> {
        if (input_.size() - cursor_ < 4) {
            return std::unexpected(failure("truncated interactive value"));
        }
        auto value = std::uint32_t{};
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(input_[cursor_++]) << shift;
        }
        return value;
    }

    [[nodiscard]] auto u64() -> liquid::result<std::uint64_t> {
        if (input_.size() - cursor_ < 8) {
            return std::unexpected(failure("truncated interactive value"));
        }
        auto value = std::uint64_t{};
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(input_[cursor_++]) << shift;
        }
        return value;
    }

    [[nodiscard]] auto string() -> liquid::result<std::string> {
        auto length = u32();
        if (!length) {
            return std::unexpected(length.error());
        }
        if (*length > input_.size() - cursor_) {
            return std::unexpected(failure("truncated interactive string"));
        }
        auto output = text(input_.subspan(cursor_, *length));
        cursor_ += *length;
        return output;
    }

    [[nodiscard]] auto range() -> liquid::result<SourceRange> {
        auto start_byte = u32();
        auto end_byte = u32();
        auto start_line = u32();
        auto start_column = u32();
        auto end_line = u32();
        auto end_column = u32();
        if (!start_byte || !end_byte || !start_line || !start_column || !end_line || !end_column) {
            return std::unexpected(failure("truncated interactive range"));
        }
        return SourceRange{
            *start_byte, *end_byte, *start_line, *start_column, *end_line, *end_column};
    }

    [[nodiscard]] auto done() const -> bool {
        return cursor_ == input_.size();
    }

private:
    std::span<const std::byte> input_;
    std::size_t cursor_{};
};

struct CursorArgument {
    std::string path;
    std::uint32_t byte{};
};

auto encode_cursor_argument(std::string_view path, std::uint32_t byte) -> std::vector<std::byte> {
    auto output = Encoder{};
    output.string(path);
    output.u32(byte);
    return std::move(output).take();
}

auto decode_cursor_argument(std::span<const std::byte> input) -> liquid::result<CursorArgument> {
    auto decoder = Decoder(input);
    auto path = decoder.string();
    auto byte = decoder.u32();
    if (!path || !byte || !decoder.done()) {
        return std::unexpected(failure("invalid cursor query argument"));
    }
    return CursorArgument{path_key(*path), *byte};
}

auto encode_path_argument(std::string_view path) -> std::vector<std::byte> {
    auto output = Encoder{};
    output.string(path);
    return std::move(output).take();
}

auto decode_path_argument(std::span<const std::byte> input) -> liquid::result<std::string> {
    auto decoder = Decoder(input);
    auto path = decoder.string();
    if (!path || !decoder.done()) {
        return std::unexpected(failure("invalid path query argument"));
    }
    return path_key(*path);
}

auto canonical_cursor_argument(std::span<const std::byte> input)
    -> liquid::result<std::vector<std::byte>> {
    auto decoded = decode_cursor_argument(input);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return encode_cursor_argument(decoded->path, decoded->byte);
}

auto canonical_path_argument(std::span<const std::byte> input)
    -> liquid::result<std::vector<std::byte>> {
    auto decoded = decode_path_argument(input);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return encode_path_argument(*decoded);
}

auto source_range(TSNode node) -> SourceRange {
    const auto start = ts_node_start_point(node);
    const auto end = ts_node_end_point(node);
    return {ts_node_start_byte(node),
            ts_node_end_byte(node),
            start.row,
            start.column,
            end.row,
            end.column};
}

auto source_text(TSNode node, std::string_view source) -> std::string_view {
    const auto start = ts_node_start_byte(node);
    const auto end = ts_node_end_byte(node);
    if (start > end || end > source.size()) {
        return {};
    }
    return source.substr(start, end - start);
}

auto normalized(std::string_view value) -> std::string {
    auto output = std::string{};
    auto previous_space = false;
    for (const char character : value) {
        if (std::isspace(static_cast<unsigned char>(character))) {
            previous_space = !output.empty();
            continue;
        }
        if (previous_space) {
            output.push_back(' ');
        }
        output.push_back(character);
        previous_space = false;
    }
    return output;
}

auto contains_word(std::string_view value, std::string_view word) -> bool {
    auto position = value.find(word);
    while (position != std::string_view::npos) {
        const auto begins =
            position == 0 || !std::isalnum(static_cast<unsigned char>(value[position - 1]));
        const auto end = position + word.size();
        const auto ends =
            end == value.size() || !std::isalnum(static_cast<unsigned char>(value[end]));
        if (begins && ends) {
            return true;
        }
        position = value.find(word, position + 1);
    }
    return false;
}

auto declaration_modifiers(std::string_view signature) -> std::vector<std::string> {
    constexpr auto candidates = std::array{"static",
                                           "extern",
                                           "inline",
                                           "const",
                                           "volatile",
                                           "virtual",
                                           "explicit",
                                           "constexpr",
                                           "noexcept"};
    auto output = std::vector<std::string>{};
    for (const auto candidate : candidates) {
        if (contains_word(signature, candidate)) {
            output.emplace_back(candidate);
        }
    }
    return output;
}

auto is_name_node(TSNode node) -> bool {
    const auto type = std::string_view(ts_node_type(node));
    return type == "identifier" || type == "field_identifier" || type == "type_identifier" ||
           type == "namespace_identifier";
}

auto declaration_name_node(TSNode node) -> TSNode {
    if (is_name_node(node)) {
        return node;
    }
    const auto count = ts_node_named_child_count(node);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto found = declaration_name_node(ts_node_named_child(node, index));
        if (!ts_node_is_null(found)) {
            return found;
        }
    }
    return {};
}

auto leading_trivia(std::string_view source, std::uint32_t start) -> std::string {
    auto begin = static_cast<std::size_t>(start);
    while (begin > 0 && std::isspace(static_cast<unsigned char>(source[begin - 1]))) {
        --begin;
    }
    if (begin >= 2 && source.substr(begin - 2, 2) == "*/") {
        const auto comment = source.rfind("/*", begin - 2);
        if (comment != std::string_view::npos) {
            begin = comment;
        }
    } else {
        while (begin >= 2) {
            const auto line_start = source.rfind('\n', begin - 1);
            const auto start_index = line_start == std::string_view::npos ? 0 : line_start + 1;
            auto line = source.substr(start_index, begin - start_index);
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
                line.remove_prefix(1);
            }
            if (!line.starts_with("//")) {
                break;
            }
            begin = start_index;
            if (begin == 0) {
                break;
            }
            --begin;
            while (begin > 0 && std::isspace(static_cast<unsigned char>(source[begin - 1]))) {
                --begin;
            }
        }
    }
    return std::string(source.substr(begin, start - begin));
}

auto documentation_from_trivia(std::string_view trivia) -> std::string {
    auto value = normalized(trivia);
    if (value.starts_with("/**")) {
        value.erase(0, 3);
    } else if (value.starts_with("/*")) {
        value.erase(0, 2);
    }
    if (value.ends_with("*/")) {
        value.resize(value.size() - 2);
    }
    while (value.starts_with("//")) {
        value.erase(0, 2);
    }
    return normalized(value);
}

auto range_contains(const SourceRange& range, std::uint32_t byte) -> bool {
    if (range.start_byte == range.end_byte) {
        return byte == range.start_byte;
    }
    return range.start_byte <= byte && byte < range.end_byte;
}

auto ranges_intersect(const SourceRange& left, const SourceRange& right) -> bool {
    return left.start_byte < right.end_byte && right.start_byte < left.end_byte;
}

auto is_definition_type(std::string_view type) -> bool {
    return type == "function_definition" || type == "class_specifier" ||
           type == "struct_specifier" || type == "enum_specifier" || type == "namespace_definition";
}

auto is_scope_type(std::string_view type) -> bool {
    return type == "translation_unit" || type == "compound_statement" ||
           type == "class_specifier" || type == "struct_specifier" ||
           type == "namespace_definition";
}

struct SyntaxEntry {
    std::string kind;
    SourceRange range;
    std::size_t parent{no_parent};
    std::uint32_t depth{};
    bool has_error{};
    bool leaf{};
};

struct DeclarationEntry {
    StubDeclaration declaration;
    SourceRange scope;
    std::uint32_t scope_depth{};
};

struct IdentifierEntry {
    std::string name;
    SourceRange range;
};

struct BufferSnapshot {
    std::shared_ptr<const std::string> source;
    std::vector<SyntaxEntry> nodes;
    std::vector<DeclarationEntry> declarations;
    std::vector<IdentifierEntry> identifiers;
};

class SnapshotBuilder {
public:
    SnapshotBuilder(std::shared_ptr<const std::string> source, TSNode root)
        : source_(std::move(source)), root_(root) {}

    [[nodiscard]] auto build() -> std::shared_ptr<const BufferSnapshot> {
        output_.source = source_;
        visit(root_, no_parent, 0, source_range(root_), 0);
        return std::make_shared<const BufferSnapshot>(std::move(output_));
    }

private:
    void add_declaration(DeclarationKind kind,
                         TSNode node,
                         TSNode name_node,
                         SourceRange scope,
                         std::uint32_t scope_depth) {
        if (ts_node_is_null(name_node)) {
            return;
        }
        const auto name = source_text(name_node, *source_);
        if (name.empty()) {
            return;
        }
        auto declaration_text = source_text(node, *source_);
        if (kind == DeclarationKind::function) {
            const auto body = declaration_text.find('{');
            if (body != std::string_view::npos) {
                declaration_text = declaration_text.substr(0, body);
            }
        }
        const auto signature = normalized(declaration_text);
        const auto trivia = leading_trivia(*source_, ts_node_start_byte(node));
        output_.declarations.push_back({{kind,
                                         std::string(name),
                                         declaration_modifiers(signature),
                                         signature,
                                         documentation_from_trivia(trivia),
                                         source_range(node)},
                                        scope,
                                        scope_depth});
    }

    void add_declarators(DeclarationKind kind,
                         TSNode node,
                         SourceRange scope,
                         std::uint32_t scope_depth) {
        const auto count = ts_node_named_child_count(node);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto child = ts_node_named_child(node, index);
            const auto type = std::string_view(ts_node_type(child));
            if (type == "init_declarator" || type.ends_with("declarator")) {
                auto actual_kind = kind;
                if (type == "function_declarator" ||
                    normalized(source_text(child, *source_)).find('(') != std::string::npos) {
                    actual_kind = DeclarationKind::function;
                }
                add_declaration(
                    actual_kind, node, declaration_name_node(child), scope, scope_depth);
            }
        }
    }

    void inspect_declaration(TSNode node, SourceRange scope, std::uint32_t scope_depth) {
        const auto type = std::string_view(ts_node_type(node));
        if (type == "function_definition") {
            add_declaration(DeclarationKind::function,
                            node,
                            ts_node_child_by_field_name(node, "declarator", 10),
                            scope,
                            scope_depth);
        } else if (type == "declaration" || type == "field_declaration") {
            add_declarators(type == "field_declaration" ? DeclarationKind::field
                                                        : DeclarationKind::variable,
                            node,
                            scope,
                            scope_depth);
        } else if (type == "type_definition" || type == "alias_declaration" ||
                   type == "class_specifier" || type == "struct_specifier" ||
                   type == "enum_specifier") {
            add_declaration(
                DeclarationKind::type, node, declaration_name_node(node), scope, scope_depth);
        } else if (type == "namespace_definition") {
            add_declaration(
                DeclarationKind::namespace_, node, declaration_name_node(node), scope, scope_depth);
        }
    }

    void visit(TSNode node,
               std::size_t parent,
               std::uint32_t depth,
               SourceRange scope,
               std::uint32_t scope_depth) {
        const auto named = ts_node_is_named(node);
        auto current_parent = parent;
        auto current_depth = depth;
        if (named) {
            current_parent = output_.nodes.size();
            output_.nodes.push_back(
                {ts_node_type(node),
                 source_range(node),
                 parent,
                 depth,
                 ts_node_has_error(node) || ts_node_is_error(node) || ts_node_is_missing(node),
                 ts_node_named_child_count(node) == 0});
            current_depth = depth + 1;
            const auto type = std::string_view(ts_node_type(node));
            if (is_name_node(node)) {
                output_.identifiers.push_back(
                    {std::string(source_text(node, *source_)), source_range(node)});
            }
            inspect_declaration(node, scope, scope_depth);
            if (is_scope_type(type)) {
                scope = source_range(node);
                scope_depth = depth;
            }
        }

        const auto count = ts_node_child_count(node);
        for (std::uint32_t index = 0; index < count; ++index) {
            visit(ts_node_child(node, index), current_parent, current_depth, scope, scope_depth);
        }
    }

    std::shared_ptr<const std::string> source_;
    TSNode root_;
    BufferSnapshot output_;
};

struct BufferState {
    StubLanguage language{};
    TSParser* parser{};
    TSTree* tree{};
    std::shared_ptr<const BufferSnapshot> snapshot;

    ~BufferState() {
        if (tree != nullptr) {
            ts_tree_delete(tree);
        }
        if (parser != nullptr) {
            ts_parser_delete(parser);
        }
    }

    BufferState() = default;
    BufferState(const BufferState&) = delete;
    auto operator=(const BufferState&) -> BufferState& = delete;
};

struct SharedState {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<BufferState>> buffers;
};

auto grammar(StubLanguage language) -> const TSLanguage* {
    return language == StubLanguage::c ? tree_sitter_c() : tree_sitter_cpp();
}

auto make_buffer(StubLanguage language, std::string source)
    -> liquid::result<std::unique_ptr<BufferState>> {
    auto output = std::make_unique<BufferState>();
    output->language = language;
    output->parser = ts_parser_new();
    if (output->parser == nullptr) {
        return std::unexpected(failure("could not create tree-sitter parser"));
    }
    if (!ts_parser_set_language(output->parser, grammar(language))) {
        return std::unexpected(failure("tree-sitter grammar is incompatible with its runtime"));
    }
    output->tree = ts_parser_parse_string(
        output->parser, nullptr, source.data(), static_cast<std::uint32_t>(source.size()));
    if (output->tree == nullptr) {
        return std::unexpected(failure("could not parse interactive buffer"));
    }
    auto source_owner = std::make_shared<const std::string>(std::move(source));
    output->snapshot = SnapshotBuilder(source_owner, ts_tree_root_node(output->tree)).build();
    return output;
}

auto overlay_input(std::string path) -> liquid::graph_input_key {
    return {liquid::graph_input_kind::overlay_buffer, std::move(path)};
}

auto open_buffers_input() -> liquid::graph_input_key {
    return {liquid::graph_input_kind::overlay_buffer, open_buffers_input_name};
}

auto analysis_index_input() -> liquid::graph_input_key {
    return {liquid::graph_input_kind::working_tree_blob, analysis_index_input_name};
}

auto translation_units_input() -> liquid::graph_input_key {
    return {liquid::graph_input_kind::working_tree_blob, translation_units_input_name};
}

auto diagnostics_input(std::string_view translation_unit) -> liquid::graph_input_key {
    return {liquid::graph_input_kind::working_tree_blob,
            "liquify.diagnostics:" + std::string(translation_unit)};
}

auto revision_bytes(std::uint64_t revision) -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u64(revision);
    return std::move(output).take();
}

auto snapshot_for(const std::shared_ptr<SharedState>& state, std::string_view path)
    -> liquid::result<std::shared_ptr<const BufferSnapshot>> {
    auto lock = std::scoped_lock(state->mutex);
    const auto found = state->buffers.find(std::string(path));
    if (found == state->buffers.end()) {
        return std::unexpected(failure("interactive buffer is not open", std::string(path)));
    }
    return found->second->snapshot;
}

auto all_snapshots(const std::shared_ptr<SharedState>& state)
    -> std::vector<std::pair<std::string, std::shared_ptr<const BufferSnapshot>>> {
    auto output = std::vector<std::pair<std::string, std::shared_ptr<const BufferSnapshot>>>{};
    auto lock = std::scoped_lock(state->mutex);
    output.reserve(state->buffers.size());
    for (const auto& [path, buffer] : state->buffers) {
        output.emplace_back(path, buffer->snapshot);
    }
    return output;
}

void encode_declaration(Encoder& output, const StubDeclaration& declaration) {
    output.u8(static_cast<std::uint8_t>(declaration.kind));
    output.string(declaration.name);
    output.u32(static_cast<std::uint32_t>(declaration.modifiers.size()));
    for (const auto& modifier : declaration.modifiers) {
        output.string(modifier);
    }
    output.string(declaration.signature);
    output.string(declaration.documentation);
    output.range(declaration.range);
}

auto decode_declaration(Decoder& input) -> liquid::result<StubDeclaration> {
    auto kind = input.u8();
    auto name = input.string();
    auto modifier_count = input.u32();
    if (!kind || !name || !modifier_count) {
        return std::unexpected(failure("truncated declaration"));
    }
    auto modifiers = std::vector<std::string>{};
    modifiers.reserve(*modifier_count);
    for (std::uint32_t index = 0; index < *modifier_count; ++index) {
        auto modifier = input.string();
        if (!modifier) {
            return std::unexpected(modifier.error());
        }
        modifiers.push_back(std::move(*modifier));
    }
    auto signature = input.string();
    auto documentation = input.string();
    auto range = input.range();
    if (!signature || !documentation || !range) {
        return std::unexpected(failure("truncated declaration"));
    }
    return StubDeclaration{static_cast<DeclarationKind>(*kind),
                           std::move(*name),
                           std::move(modifiers),
                           std::move(*signature),
                           std::move(*documentation),
                           *range};
}

void encode_symbol_location(Encoder& output, const SymbolLocation& location) {
    output.string(path_key(location.path));
    encode_declaration(output, location.declaration);
}

auto decode_symbol_location(Decoder& input) -> liquid::result<SymbolLocation> {
    auto path = input.string();
    auto declaration = decode_declaration(input);
    if (!path || !declaration) {
        return std::unexpected(failure("truncated symbol location"));
    }
    return SymbolLocation{std::filesystem::path(*path), std::move(*declaration)};
}

void encode_source_location(Encoder& output, const SourceLocation& location) {
    output.string(path_key(location.path));
    output.range(location.range);
}

auto decode_source_location(Decoder& input) -> liquid::result<SourceLocation> {
    auto path = input.string();
    auto range = input.range();
    if (!path || !range) {
        return std::unexpected(failure("truncated source location"));
    }
    return SourceLocation{std::filesystem::path(*path), *range};
}

struct AnalysisIndex {
    std::vector<std::pair<std::string, StubTree>> stubs;
    std::vector<SymbolRecord> symbols;
};

auto encode_analysis_index(const AnalysisIndex& index) -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u8(1);
    output.u32(static_cast<std::uint32_t>(index.stubs.size()));
    for (const auto& [path, tree] : index.stubs) {
        output.string(path);
        output.u32(static_cast<std::uint32_t>(tree.declarations.size()));
        for (const auto& declaration : tree.declarations) {
            encode_declaration(output, declaration);
        }
    }
    output.u32(static_cast<std::uint32_t>(index.symbols.size()));
    for (const auto& symbol : index.symbols) {
        output.string(symbol.name);
        encode_symbol_location(output, symbol.definition);
        output.u32(static_cast<std::uint32_t>(symbol.references.size()));
        for (const auto& reference : symbol.references) {
            encode_source_location(output, reference);
        }
    }
    return std::move(output).take();
}

auto decode_analysis_index(std::span<const std::byte> input) -> liquid::result<AnalysisIndex> {
    auto decoder = Decoder(input);
    auto version = decoder.u8();
    auto stub_count = decoder.u32();
    if (!version || *version != 1 || !stub_count) {
        return std::unexpected(failure("unsupported interactive index"));
    }
    auto output = AnalysisIndex{};
    output.stubs.reserve(*stub_count);
    for (std::uint32_t stub_index = 0; stub_index < *stub_count; ++stub_index) {
        auto path = decoder.string();
        auto declaration_count = decoder.u32();
        if (!path || !declaration_count) {
            return std::unexpected(failure("truncated interactive stub index"));
        }
        auto tree = StubTree{};
        tree.declarations.reserve(*declaration_count);
        for (std::uint32_t declaration_index = 0; declaration_index < *declaration_count;
             ++declaration_index) {
            auto declaration = decode_declaration(decoder);
            if (!declaration) {
                return std::unexpected(declaration.error());
            }
            tree.declarations.push_back(std::move(*declaration));
        }
        output.stubs.emplace_back(path_key(*path), std::move(tree));
    }
    auto symbol_count = decoder.u32();
    if (!symbol_count) {
        return std::unexpected(symbol_count.error());
    }
    output.symbols.reserve(*symbol_count);
    for (std::uint32_t symbol_index = 0; symbol_index < *symbol_count; ++symbol_index) {
        auto name = decoder.string();
        auto definition = decode_symbol_location(decoder);
        auto reference_count = decoder.u32();
        if (!name || !definition || !reference_count) {
            return std::unexpected(failure("truncated symbol record"));
        }
        auto references = std::vector<SourceLocation>{};
        references.reserve(*reference_count);
        for (std::uint32_t reference_index = 0; reference_index < *reference_count;
             ++reference_index) {
            auto reference = decode_source_location(decoder);
            if (!reference) {
                return std::unexpected(reference.error());
            }
            references.push_back(std::move(*reference));
        }
        output.symbols.push_back({std::move(*name), std::move(*definition), std::move(references)});
    }
    if (!decoder.done()) {
        return std::unexpected(failure("trailing interactive index bytes"));
    }
    return output;
}

auto encode_translation_units(const std::map<std::string, std::string>& translation_units)
    -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u8(1);
    output.u32(static_cast<std::uint32_t>(translation_units.size()));
    for (const auto& [buffer, translation_unit] : translation_units) {
        output.string(buffer);
        output.string(translation_unit);
    }
    return std::move(output).take();
}

auto decode_translation_units(std::span<const std::byte> input)
    -> liquid::result<std::map<std::string, std::string>> {
    auto decoder = Decoder(input);
    auto version = decoder.u8();
    auto count = decoder.u32();
    if (!version || *version != 1 || !count) {
        return std::unexpected(failure("unsupported translation-unit index"));
    }
    auto output = std::map<std::string, std::string>{};
    for (std::uint32_t index = 0; index < *count; ++index) {
        auto buffer = decoder.string();
        auto translation_unit = decoder.string();
        if (!buffer || !translation_unit) {
            return std::unexpected(failure("truncated translation-unit index"));
        }
        output.emplace(path_key(*buffer), path_key(*translation_unit));
    }
    if (!decoder.done()) {
        return std::unexpected(failure("trailing translation-unit index bytes"));
    }
    return output;
}

auto encode_diagnostics(std::span<const Diagnostic> diagnostics) -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u8(1);
    output.u32(static_cast<std::uint32_t>(diagnostics.size()));
    for (const auto& diagnostic : diagnostics) {
        encode_source_location(output, diagnostic.location);
        output.u8(static_cast<std::uint8_t>(diagnostic.severity));
        output.string(diagnostic.message);
    }
    return std::move(output).take();
}

auto decode_diagnostics(std::span<const std::byte> input)
    -> liquid::result<std::vector<Diagnostic>> {
    auto decoder = Decoder(input);
    auto version = decoder.u8();
    auto count = decoder.u32();
    if (!version || *version != 1 || !count) {
        return std::unexpected(failure("unsupported diagnostics value"));
    }
    auto output = std::vector<Diagnostic>{};
    output.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        auto location = decode_source_location(decoder);
        auto severity = decoder.u8();
        auto message = decoder.string();
        if (!location || !severity || !message) {
            return std::unexpected(failure("truncated diagnostic"));
        }
        output.push_back({std::move(*location),
                          static_cast<DiagnosticSeverity>(*severity),
                          std::move(*message)});
    }
    if (!decoder.done()) {
        return std::unexpected(failure("trailing diagnostics bytes"));
    }
    return output;
}

auto encode_syntax_node(const std::optional<SyntaxNodeInfo>& node) -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u8(node.has_value() ? 1 : 0);
    if (node) {
        output.string(node->kind);
        output.string(node->spelling);
        output.string(node->leading_trivia);
        output.range(node->range);
        output.u8(node->has_error ? 1 : 0);
    }
    return std::move(output).take();
}

auto decode_syntax_node(std::span<const std::byte> input)
    -> liquid::result<std::optional<SyntaxNodeInfo>> {
    auto decoder = Decoder(input);
    auto present = decoder.u8();
    if (!present) {
        return std::unexpected(present.error());
    }
    if (*present == 0) {
        if (!decoder.done()) {
            return std::unexpected(failure("trailing syntax-node bytes"));
        }
        return std::optional<SyntaxNodeInfo>{};
    }
    auto kind = decoder.string();
    auto spelling = decoder.string();
    auto trivia = decoder.string();
    auto range = decoder.range();
    auto has_error = decoder.u8();
    if (!kind || !spelling || !trivia || !range || !has_error || !decoder.done()) {
        return std::unexpected(failure("truncated syntax node"));
    }
    return std::optional<SyntaxNodeInfo>(SyntaxNodeInfo{
        std::move(*kind), std::move(*spelling), std::move(*trivia), *range, *has_error != 0});
}

auto encode_optional_symbol(const std::optional<SymbolLocation>& location)
    -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u8(location.has_value() ? 1 : 0);
    if (location) {
        encode_symbol_location(output, *location);
    }
    return std::move(output).take();
}

auto decode_optional_symbol(std::span<const std::byte> input)
    -> liquid::result<std::optional<SymbolLocation>> {
    auto decoder = Decoder(input);
    auto present = decoder.u8();
    if (!present) {
        return std::unexpected(present.error());
    }
    if (*present == 0) {
        if (!decoder.done()) {
            return std::unexpected(failure("trailing symbol bytes"));
        }
        return std::optional<SymbolLocation>{};
    }
    auto location = decode_symbol_location(decoder);
    if (!location || !decoder.done()) {
        return std::unexpected(failure("truncated symbol"));
    }
    return std::optional<SymbolLocation>(std::move(*location));
}

auto encode_references(std::span<const ReferenceLocation> references) -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u32(static_cast<std::uint32_t>(references.size()));
    for (const auto& reference : references) {
        encode_source_location(output, reference.location);
        output.u8(reference.is_definition ? 1 : 0);
    }
    return std::move(output).take();
}

auto decode_references(std::span<const std::byte> input)
    -> liquid::result<std::vector<ReferenceLocation>> {
    auto decoder = Decoder(input);
    auto count = decoder.u32();
    if (!count) {
        return std::unexpected(count.error());
    }
    auto output = std::vector<ReferenceLocation>{};
    output.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        auto location = decode_source_location(decoder);
        auto is_definition = decoder.u8();
        if (!location || !is_definition) {
            return std::unexpected(failure("truncated reference"));
        }
        output.push_back({std::move(*location), *is_definition != 0});
    }
    if (!decoder.done()) {
        return std::unexpected(failure("trailing reference bytes"));
    }
    return output;
}

auto encode_hover(const std::optional<HoverInfo>& hover) -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u8(hover.has_value() ? 1 : 0);
    if (hover) {
        output.string(hover->signature);
        output.string(hover->documentation);
        encode_symbol_location(output, hover->definition);
    }
    return std::move(output).take();
}

auto decode_hover(std::span<const std::byte> input) -> liquid::result<std::optional<HoverInfo>> {
    auto decoder = Decoder(input);
    auto present = decoder.u8();
    if (!present) {
        return std::unexpected(present.error());
    }
    if (*present == 0) {
        if (!decoder.done()) {
            return std::unexpected(failure("trailing hover bytes"));
        }
        return std::optional<HoverInfo>{};
    }
    auto signature = decoder.string();
    auto documentation = decoder.string();
    auto definition = decode_symbol_location(decoder);
    if (!signature || !documentation || !definition || !decoder.done()) {
        return std::unexpected(failure("truncated hover value"));
    }
    return std::optional<HoverInfo>(
        HoverInfo{std::move(*signature), std::move(*documentation), std::move(*definition)});
}

auto encode_completions(std::span<const CompletionCandidate> completions)
    -> std::vector<std::byte> {
    auto output = Encoder{};
    output.u32(static_cast<std::uint32_t>(completions.size()));
    for (const auto& completion : completions) {
        output.string(completion.name);
        output.u8(static_cast<std::uint8_t>(completion.kind));
        output.string(completion.signature);
        output.string(path_key(completion.path));
        output.u32(completion.rank);
    }
    return std::move(output).take();
}

auto decode_completions(std::span<const std::byte> input)
    -> liquid::result<std::vector<CompletionCandidate>> {
    auto decoder = Decoder(input);
    auto count = decoder.u32();
    if (!count) {
        return std::unexpected(count.error());
    }
    auto output = std::vector<CompletionCandidate>{};
    output.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        auto name = decoder.string();
        auto kind = decoder.u8();
        auto signature = decoder.string();
        auto path = decoder.string();
        auto rank = decoder.u32();
        if (!name || !kind || !signature || !path || !rank) {
            return std::unexpected(failure("truncated completion"));
        }
        output.push_back({std::move(*name),
                          static_cast<DeclarationKind>(*kind),
                          std::move(*signature),
                          std::filesystem::path(*path),
                          *rank});
    }
    if (!decoder.done()) {
        return std::unexpected(failure("trailing completion bytes"));
    }
    return output;
}

auto syntax_node_at(const BufferSnapshot& snapshot, std::uint32_t byte)
    -> std::optional<std::size_t> {
    auto best = std::optional<std::size_t>{};
    for (std::size_t index = 0; index < snapshot.nodes.size(); ++index) {
        const auto& node = snapshot.nodes[index];
        if (!range_contains(node.range, byte)) {
            continue;
        }
        if (!best || node.depth >= snapshot.nodes[*best].depth) {
            best = index;
        }
    }
    return best;
}

auto syntax_node_info(const BufferSnapshot& snapshot, std::size_t index) -> SyntaxNodeInfo {
    const auto& node = snapshot.nodes[index];
    const auto size = node.range.end_byte - node.range.start_byte;
    return {node.kind,
            snapshot.source->substr(node.range.start_byte, size),
            leading_trivia(*snapshot.source, node.range.start_byte),
            node.range,
            node.has_error};
}

auto node_query(const BufferSnapshot& snapshot, std::uint32_t byte, bool enclosing)
    -> std::optional<SyntaxNodeInfo> {
    auto found = syntax_node_at(snapshot, byte);
    if (!found) {
        return std::nullopt;
    }
    if (enclosing) {
        while (!is_definition_type(snapshot.nodes[*found].kind)) {
            const auto parent = snapshot.nodes[*found].parent;
            if (parent == no_parent) {
                return std::nullopt;
            }
            found = parent;
        }
    }
    return syntax_node_info(snapshot, *found);
}

auto is_identifier_character(char character) -> bool {
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

auto word_at(const BufferSnapshot& snapshot, std::uint32_t byte) -> std::string {
    if (snapshot.source->empty()) {
        return {};
    }
    auto position = std::min<std::size_t>(byte, snapshot.source->size() - 1);
    if (!is_identifier_character((*snapshot.source)[position]) && position > 0 &&
        is_identifier_character((*snapshot.source)[position - 1])) {
        --position;
    }
    if (!is_identifier_character((*snapshot.source)[position])) {
        return {};
    }
    auto begin = position;
    while (begin > 0 && is_identifier_character((*snapshot.source)[begin - 1])) {
        --begin;
    }
    auto end = position + 1;
    while (end < snapshot.source->size() && is_identifier_character((*snapshot.source)[end])) {
        ++end;
    }
    return snapshot.source->substr(begin, end - begin);
}

auto definition_from_symbols(const AnalysisIndex& index, std::string_view name)
    -> std::optional<SymbolLocation> {
    const auto found = std::ranges::find(index.symbols, name, &SymbolRecord::name);
    if (found == index.symbols.end()) {
        return std::nullopt;
    }
    return found->definition;
}

auto definition_from_open_buffers(
    std::span<const std::pair<std::string, std::shared_ptr<const BufferSnapshot>>> snapshots,
    std::string_view preferred_path,
    std::string_view name) -> std::optional<SymbolLocation> {
    for (const auto& [path, snapshot] : snapshots) {
        if (path != preferred_path) {
            continue;
        }
        for (const auto& entry : snapshot->declarations) {
            if (entry.declaration.name == name) {
                return SymbolLocation{path, entry.declaration};
            }
        }
    }
    for (const auto& [path, snapshot] : snapshots) {
        if (path == preferred_path) {
            continue;
        }
        for (const auto& entry : snapshot->declarations) {
            if (entry.declaration.name == name) {
                return SymbolLocation{path, entry.declaration};
            }
        }
    }
    return std::nullopt;
}

auto definition_from_stubs(const AnalysisIndex& index, std::string_view name)
    -> std::optional<SymbolLocation> {
    for (const auto& [path, tree] : index.stubs) {
        for (const auto& declaration : tree.declarations) {
            if (declaration.name == name) {
                return SymbolLocation{path, declaration};
            }
        }
    }
    return std::nullopt;
}

auto find_definition(
    const AnalysisIndex& index,
    std::span<const std::pair<std::string, std::shared_ptr<const BufferSnapshot>>> snapshots,
    std::string_view preferred_path,
    std::string_view name) -> std::optional<SymbolLocation> {
    auto definition = definition_from_symbols(index, name);
    if (definition) {
        return definition;
    }
    definition = definition_from_open_buffers(snapshots, preferred_path, name);
    if (definition) {
        return definition;
    }
    return definition_from_stubs(index, name);
}

auto location_token(const SourceLocation& location) -> std::string {
    return path_key(location.path) + ":" + std::to_string(location.range.start_byte) + ":" +
           std::to_string(location.range.end_byte);
}

auto find_symbol_record(const AnalysisIndex& index, std::string_view name) -> const SymbolRecord* {
    const auto found = std::ranges::find(index.symbols, name, &SymbolRecord::name);
    return found == index.symbols.end() ? nullptr : &*found;
}

auto collect_references(
    const AnalysisIndex& index,
    std::span<const std::pair<std::string, std::shared_ptr<const BufferSnapshot>>> snapshots,
    std::string_view preferred_path,
    std::string_view name) -> std::vector<ReferenceLocation> {
    auto output = std::vector<ReferenceLocation>{};
    auto seen = std::set<std::string>{};
    if (const auto* symbol = find_symbol_record(index, name); symbol != nullptr) {
        const auto definition =
            SourceLocation{symbol->definition.path, symbol->definition.declaration.range};
        seen.insert(location_token(definition));
        output.push_back({definition, true});
        for (const auto& reference : symbol->references) {
            if (seen.insert(location_token(reference)).second) {
                output.push_back({reference, false});
            }
        }
        return output;
    }

    const auto definition = find_definition(index, snapshots, preferred_path, name);
    if (definition) {
        const auto location = SourceLocation{definition->path, definition->declaration.range};
        seen.insert(location_token(location));
        output.push_back({location, true});
    }
    for (const auto& [path, snapshot] : snapshots) {
        for (const auto& identifier : snapshot->identifiers) {
            if (identifier.name != name) {
                continue;
            }
            auto location = SourceLocation{path, identifier.range};
            if (seen.insert(location_token(location)).second) {
                output.push_back({std::move(location), false});
            }
        }
    }
    return output;
}

auto hover_for_definition(const std::optional<SymbolLocation>& definition)
    -> std::optional<HoverInfo> {
    if (!definition) {
        return std::nullopt;
    }
    return HoverInfo{
        definition->declaration.signature, definition->declaration.documentation, *definition};
}

auto completion_rank(const DeclarationEntry& entry, std::uint32_t byte) -> std::uint32_t {
    const auto distance = byte >= entry.declaration.range.start_byte
                              ? byte - entry.declaration.range.start_byte
                              : entry.declaration.range.start_byte - byte;
    const auto scope_bonus = std::min<std::uint32_t>(entry.scope_depth, 100U) * 1'000U;
    return distance > scope_bonus ? distance - scope_bonus : 0;
}

auto is_visible(const DeclarationEntry& entry, std::uint32_t byte) -> bool {
    if (!range_contains(entry.scope, byte)) {
        return false;
    }
    if (entry.scope_depth == 0) {
        return true;
    }
    return entry.declaration.range.start_byte <= byte;
}

void offer_completion(std::map<std::string, CompletionCandidate>& candidates,
                      CompletionCandidate candidate) {
    const auto found = candidates.find(candidate.name);
    if (found == candidates.end() || candidate.rank < found->second.rank) {
        candidates.insert_or_assign(candidate.name, std::move(candidate));
    }
}

auto collect_completions(
    const AnalysisIndex& index,
    std::span<const std::pair<std::string, std::shared_ptr<const BufferSnapshot>>> snapshots,
    std::string_view current_path,
    std::uint32_t byte) -> std::vector<CompletionCandidate> {
    auto candidates = std::map<std::string, CompletionCandidate>{};
    for (const auto& [path, snapshot] : snapshots) {
        for (const auto& entry : snapshot->declarations) {
            if (path == current_path && !is_visible(entry, byte)) {
                continue;
            }
            const auto rank = path == current_path ? completion_rank(entry, byte) : 100'000U;
            offer_completion(candidates,
                             {entry.declaration.name,
                              entry.declaration.kind,
                              entry.declaration.signature,
                              path,
                              rank});
        }
    }
    for (const auto& symbol : index.symbols) {
        offer_completion(candidates,
                         {symbol.name,
                          symbol.definition.declaration.kind,
                          symbol.definition.declaration.signature,
                          symbol.definition.path,
                          150'000U});
    }
    for (const auto& [path, tree] : index.stubs) {
        for (const auto& declaration : tree.declarations) {
            offer_completion(
                candidates,
                {declaration.name, declaration.kind, declaration.signature, path, 200'000U});
        }
    }
    auto output = std::vector<CompletionCandidate>{};
    output.reserve(candidates.size());
    for (auto& [name, candidate] : candidates) {
        output.push_back(std::move(candidate));
    }
    std::ranges::sort(output, [](const auto& left, const auto& right) {
        if (left.rank != right.rank) {
            return left.rank < right.rank;
        }
        return left.name < right.name;
    });
    return output;
}

auto compute_node_query(const std::shared_ptr<SharedState>& state,
                        liquid::query_context& context,
                        std::vector<std::byte> arguments,
                        bool enclosing) -> liquid::runtime::task<std::vector<std::byte>> {
    auto cursor = decode_cursor_argument(arguments);
    if (!cursor) {
        co_return std::unexpected(cursor.error());
    }
    auto input = co_await context.read(overlay_input(cursor->path));
    if (!input) {
        co_return std::unexpected(input.error());
    }
    auto snapshot = snapshot_for(state, cursor->path);
    if (!snapshot) {
        co_return std::unexpected(snapshot.error());
    }
    const auto result = node_query(**snapshot, cursor->byte, enclosing);
    auto current = co_await context.read(overlay_input(cursor->path));
    if (!current) {
        co_return std::unexpected(current.error());
    }
    co_return encode_syntax_node(result);
}

struct CrossQueryInputs {
    std::shared_ptr<const BufferSnapshot> current;
    std::vector<std::pair<std::string, std::shared_ptr<const BufferSnapshot>>> snapshots;
    AnalysisIndex index;
};

auto read_cross_query_inputs(const std::shared_ptr<SharedState>& state,
                             liquid::query_context& context,
                             const CursorArgument& cursor)
    -> liquid::runtime::task<CrossQueryInputs> {
    auto overlay = co_await context.read(overlay_input(cursor.path));
    if (!overlay) {
        co_return std::unexpected(overlay.error());
    }
    auto open_revision = co_await context.read(open_buffers_input());
    if (!open_revision) {
        co_return std::unexpected(open_revision.error());
    }
    auto encoded_index = co_await context.read(analysis_index_input());
    if (!encoded_index) {
        co_return std::unexpected(encoded_index.error());
    }
    auto index = decode_analysis_index(encoded_index->bytes);
    if (!index) {
        co_return std::unexpected(index.error());
    }
    auto current = snapshot_for(state, cursor.path);
    if (!current) {
        co_return std::unexpected(current.error());
    }
    co_return CrossQueryInputs{*current, all_snapshots(state), std::move(*index)};
}

auto finish_cross_query(liquid::query_context& context, std::string_view path)
    -> liquid::runtime::task<void> {
    auto current = co_await context.read(overlay_input(std::string(path)));
    if (!current) {
        co_return std::unexpected(current.error());
    }
    co_return liquid::result<void>{};
}

auto compute_definition_query(const std::shared_ptr<SharedState>& state,
                              liquid::query_context& context,
                              std::vector<std::byte> arguments)
    -> liquid::runtime::task<std::vector<std::byte>> {
    auto cursor = decode_cursor_argument(arguments);
    if (!cursor) {
        co_return std::unexpected(cursor.error());
    }
    auto inputs = co_await read_cross_query_inputs(state, context, *cursor);
    if (!inputs) {
        co_return std::unexpected(inputs.error());
    }
    const auto name = word_at(*inputs->current, cursor->byte);
    const auto result = find_definition(inputs->index, inputs->snapshots, cursor->path, name);
    auto finished = co_await finish_cross_query(context, cursor->path);
    if (!finished) {
        co_return std::unexpected(finished.error());
    }
    co_return encode_optional_symbol(result);
}

auto compute_references_query(const std::shared_ptr<SharedState>& state,
                              liquid::query_context& context,
                              std::vector<std::byte> arguments)
    -> liquid::runtime::task<std::vector<std::byte>> {
    auto cursor = decode_cursor_argument(arguments);
    if (!cursor) {
        co_return std::unexpected(cursor.error());
    }
    auto inputs = co_await read_cross_query_inputs(state, context, *cursor);
    if (!inputs) {
        co_return std::unexpected(inputs.error());
    }
    const auto name = word_at(*inputs->current, cursor->byte);
    const auto result = collect_references(inputs->index, inputs->snapshots, cursor->path, name);
    auto finished = co_await finish_cross_query(context, cursor->path);
    if (!finished) {
        co_return std::unexpected(finished.error());
    }
    co_return encode_references(result);
}

auto compute_hover_query(const std::shared_ptr<SharedState>& state,
                         liquid::query_context& context,
                         std::vector<std::byte> arguments)
    -> liquid::runtime::task<std::vector<std::byte>> {
    auto cursor = decode_cursor_argument(arguments);
    if (!cursor) {
        co_return std::unexpected(cursor.error());
    }
    auto inputs = co_await read_cross_query_inputs(state, context, *cursor);
    if (!inputs) {
        co_return std::unexpected(inputs.error());
    }
    const auto name = word_at(*inputs->current, cursor->byte);
    const auto definition = find_definition(inputs->index, inputs->snapshots, cursor->path, name);
    const auto result = hover_for_definition(definition);
    auto finished = co_await finish_cross_query(context, cursor->path);
    if (!finished) {
        co_return std::unexpected(finished.error());
    }
    co_return encode_hover(result);
}

auto compute_completion_query(const std::shared_ptr<SharedState>& state,
                              liquid::query_context& context,
                              std::vector<std::byte> arguments)
    -> liquid::runtime::task<std::vector<std::byte>> {
    auto cursor = decode_cursor_argument(arguments);
    if (!cursor) {
        co_return std::unexpected(cursor.error());
    }
    auto inputs = co_await read_cross_query_inputs(state, context, *cursor);
    if (!inputs) {
        co_return std::unexpected(inputs.error());
    }
    const auto result =
        collect_completions(inputs->index, inputs->snapshots, cursor->path, cursor->byte);
    auto finished = co_await finish_cross_query(context, cursor->path);
    if (!finished) {
        co_return std::unexpected(finished.error());
    }
    co_return encode_completions(result);
}

auto compute_diagnostics_query(const std::shared_ptr<SharedState>& state,
                               liquid::query_context& context,
                               std::vector<std::byte> arguments)
    -> liquid::runtime::task<std::vector<std::byte>> {
    auto path = decode_path_argument(arguments);
    if (!path) {
        co_return std::unexpected(path.error());
    }
    auto overlay = co_await context.read(overlay_input(*path));
    if (!overlay) {
        co_return std::unexpected(overlay.error());
    }
    auto mapping_value = co_await context.read(translation_units_input());
    if (!mapping_value) {
        co_return std::unexpected(mapping_value.error());
    }
    auto mapping = decode_translation_units(mapping_value->bytes);
    if (!mapping) {
        co_return std::unexpected(mapping.error());
    }
    const auto found = mapping->find(*path);
    if (found == mapping->end()) {
        co_return encode_diagnostics({});
    }
    auto diagnostics_value = co_await context.read(diagnostics_input(found->second));
    if (!diagnostics_value) {
        co_return std::unexpected(diagnostics_value.error());
    }
    auto diagnostics = decode_diagnostics(diagnostics_value->bytes);
    if (!diagnostics) {
        co_return std::unexpected(diagnostics.error());
    }
    auto current = co_await context.read(overlay_input(*path));
    if (!current) {
        co_return std::unexpected(current.error());
    }
    co_return encode_diagnostics(*diagnostics);
}

auto interactive_registry(const std::shared_ptr<SharedState>& state)
    -> liquid::result<liquid::query_registry> {
    auto registry = liquid::query_registry{};
    const auto add = [&registry](liquid::query_kind kind) -> liquid::result<void> {
        return registry.add(std::move(kind));
    };

    auto added = add(
        {.name = std::string(interactive_query_kind_name(InteractiveQueryKind::node_at_position)),
         .encode_arguments = canonical_cursor_argument,
         .compute = [state](auto& context, auto arguments) {
             return compute_node_query(state, context, std::move(arguments), false);
         }});
    if (!added) {
        return std::unexpected(added.error());
    }
    added = add({.name = std::string(
                     interactive_query_kind_name(InteractiveQueryKind::enclosing_definition)),
                 .encode_arguments = canonical_cursor_argument,
                 .compute = [state](auto& context, auto arguments) {
                     return compute_node_query(state, context, std::move(arguments), true);
                 }});
    if (!added) {
        return std::unexpected(added.error());
    }
    added = add(
        {.name = std::string(interactive_query_kind_name(InteractiveQueryKind::go_to_definition)),
         .encode_arguments = canonical_cursor_argument,
         .compute = [state](auto& context, auto arguments) {
             return compute_definition_query(state, context, std::move(arguments));
         }});
    if (!added) {
        return std::unexpected(added.error());
    }
    added = add(
        {.name = std::string(interactive_query_kind_name(InteractiveQueryKind::find_references)),
         .encode_arguments = canonical_cursor_argument,
         .compute = [state](auto& context, auto arguments) {
             return compute_references_query(state, context, std::move(arguments));
         }});
    if (!added) {
        return std::unexpected(added.error());
    }
    added = add({.name = std::string(interactive_query_kind_name(InteractiveQueryKind::hover)),
                 .encode_arguments = canonical_cursor_argument,
                 .compute = [state](auto& context, auto arguments) {
                     return compute_hover_query(state, context, std::move(arguments));
                 }});
    if (!added) {
        return std::unexpected(added.error());
    }
    added = add({.name = std::string(interactive_query_kind_name(InteractiveQueryKind::completion)),
                 .encode_arguments = canonical_cursor_argument,
                 .compute = [state](auto& context, auto arguments) {
                     return compute_completion_query(state, context, std::move(arguments));
                 }});
    if (!added) {
        return std::unexpected(added.error());
    }
    added =
        add({.name = std::string(interactive_query_kind_name(InteractiveQueryKind::diagnostics)),
             .encode_arguments = canonical_path_argument,
             .compute = [state](auto& context, auto arguments) {
                 return compute_diagnostics_query(state, context, std::move(arguments));
             }});
    if (!added) {
        return std::unexpected(added.error());
    }
    return registry;
}

auto point_at(std::string_view source, std::uint32_t byte) -> TSPoint {
    auto row = std::uint32_t{};
    auto column = std::uint32_t{};
    const auto end = std::min<std::size_t>(byte, source.size());
    for (std::size_t index = 0; index < end; ++index) {
        if (source[index] == '\n') {
            ++row;
            column = 0;
        } else {
            ++column;
        }
    }
    return {row, column};
}

auto advanced_point(TSPoint start, std::string_view inserted) -> TSPoint {
    auto output = start;
    for (const char character : inserted) {
        if (character == '\n') {
            ++output.row;
            output.column = 0;
        } else {
            ++output.column;
        }
    }
    return output;
}

auto input_edit(std::string_view source, const SourceEdit& edit) -> TSInputEdit {
    const auto start = point_at(source, edit.start_byte);
    const auto old_end = point_at(source, edit.old_end_byte);
    const auto new_end = advanced_point(start, edit.text);
    return {edit.start_byte,
            edit.old_end_byte,
            edit.start_byte + static_cast<std::uint32_t>(edit.text.size()),
            start,
            old_end,
            new_end};
}

auto edited_source(std::string_view source, const SourceEdit& edit) -> liquid::result<std::string> {
    if (edit.start_byte > edit.old_end_byte || edit.old_end_byte > source.size()) {
        return std::unexpected(failure("interactive edit range is invalid"));
    }
    auto output = std::string(source);
    output.replace(edit.start_byte, edit.old_end_byte - edit.start_byte, edit.text);
    return output;
}

auto changed_ranges(TSTree* old_tree, TSTree* new_tree) -> std::vector<SourceRange> {
    auto count = std::uint32_t{};
    TSRange* ranges = ts_tree_get_changed_ranges(old_tree, new_tree, &count);
    auto output = std::vector<SourceRange>{};
    output.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        output.push_back({ranges[index].start_byte,
                          ranges[index].end_byte,
                          ranges[index].start_point.row,
                          ranges[index].start_point.column,
                          ranges[index].end_point.row,
                          ranges[index].end_point.column});
    }
    free(ranges);
    return output;
}

auto incremental_report(const BufferSnapshot& after,
                        const TSInputEdit& edit,
                        std::vector<SourceRange> changed) -> IncrementalParseReport {
    auto output = IncrementalParseReport{};
    output.edited_range = {edit.start_byte,
                           edit.new_end_byte,
                           edit.start_point.row,
                           edit.start_point.column,
                           edit.new_end_point.row,
                           edit.new_end_point.column};
    output.changed_ranges = std::move(changed);
    auto affected_ranges = output.changed_ranges;
    if (affected_ranges.empty()) {
        affected_ranges.push_back(output.edited_range);
    }
    for (const auto& node : after.nodes) {
        if (!node.leaf) {
            continue;
        }
        ++output.total_leaf_nodes;
        const auto affected = std::ranges::any_of(affected_ranges, [&node](const auto& range) {
            return ranges_intersect(node.range, range);
        });
        if (!affected) {
            ++output.reused_leaf_nodes;
            continue;
        }
        output.reparsed_leaf_ranges.push_back(node.range);
    }
    return output;
}

auto percentile_95(std::vector<double> values) -> double {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto index =
        static_cast<std::size_t>(std::ceil(static_cast<double>(values.size()) * 0.95) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

} // namespace

struct InteractiveAnalysis::Impl {
    Impl(std::shared_ptr<SharedState> shared_state, liquid::graph opened_graph)
        : state(std::move(shared_state)), graph(std::move(opened_graph)) {}

    [[nodiscard]] auto set_input(liquid::graph_input_key key,
                                 std::vector<std::byte> value,
                                 liquid::durability durability) -> liquid::result<void> {
        auto stored = liquid::runtime::block_on(
            graph.set_input(std::move(key), std::move(value), durability));
        if (!stored) {
            return std::unexpected(stored.error());
        }
        return {};
    }

    [[nodiscard]] auto bump_open_buffers() -> liquid::result<void> {
        ++open_revision;
        return set_input(
            open_buffers_input(), revision_bytes(open_revision), liquid::durability::volatile_);
    }

    [[nodiscard]] auto demand(InteractiveQueryKind kind, std::vector<std::byte> arguments)
        -> liquid::result<std::vector<std::byte>> {
        auto query_path = std::string{};
        if (kind == InteractiveQueryKind::diagnostics) {
            auto decoded = decode_path_argument(arguments);
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            query_path = std::move(*decoded);
        } else {
            auto decoded = decode_cursor_argument(arguments);
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            query_path = std::move(decoded->path);
        }
        auto key =
            liquid::query_key{std::string(interactive_query_kind_name(kind)), std::move(arguments)};
        {
            auto lock = std::scoped_lock(state->mutex);
            active_queries[query_path].push_back(key);
        }
        auto value = liquid::runtime::block_on(graph.demand(key));
        {
            auto lock = std::scoped_lock(state->mutex);
            const auto found = active_queries.find(query_path);
            if (found != active_queries.end()) {
                const auto active = std::ranges::find(found->second, key);
                if (active != found->second.end()) {
                    found->second.erase(active);
                }
                if (found->second.empty()) {
                    active_queries.erase(found);
                }
            }
        }
        if (!value) {
            return std::unexpected(value.error());
        }
        return std::move(value->bytes);
    }

    void cancel_queries(std::string_view path) {
        const auto found = active_queries.find(std::string(path));
        if (found == active_queries.end()) {
            return;
        }
        for (const auto& key : found->second) {
            static_cast<void>(graph.mark_changed(key));
        }
    }

    std::shared_ptr<SharedState> state;
    liquid::graph graph;
    AnalysisIndex index;
    std::map<std::string, std::string> translation_units;
    std::map<std::string, std::vector<liquid::query_key>> active_queries;
    std::uint64_t open_revision{};
};

InteractiveAnalysis::InteractiveAnalysis(std::unique_ptr<InteractiveAnalysis::Impl> implementation)
    : impl_(std::move(implementation)) {}

InteractiveAnalysis::InteractiveAnalysis(InteractiveAnalysis&&) noexcept = default;

auto InteractiveAnalysis::operator=(InteractiveAnalysis&&) noexcept
    -> InteractiveAnalysis& = default;

InteractiveAnalysis::~InteractiveAnalysis() = default;

auto InteractiveAnalysis::open(std::filesystem::path state_directory)
    -> liquid::result<InteractiveAnalysis> {
    auto state = std::make_shared<SharedState>();
    auto registry = interactive_registry(state);
    if (!registry) {
        return std::unexpected(registry.error());
    }
    auto graph = liquid::runtime::block_on(
        liquid::graph::open(std::move(state_directory), std::move(*registry)));
    if (!graph) {
        return std::unexpected(graph.error());
    }
    auto implementation = std::make_unique<Impl>(std::move(state), std::move(*graph));
    auto indexed = implementation->set_input(analysis_index_input(),
                                             encode_analysis_index(implementation->index),
                                             liquid::durability::low);
    if (!indexed) {
        return std::unexpected(indexed.error());
    }
    auto mapped =
        implementation->set_input(translation_units_input(),
                                  encode_translation_units(implementation->translation_units),
                                  liquid::durability::low);
    if (!mapped) {
        return std::unexpected(mapped.error());
    }
    auto opened = implementation->set_input(open_buffers_input(),
                                            revision_bytes(implementation->open_revision),
                                            liquid::durability::volatile_);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    return InteractiveAnalysis(std::move(implementation));
}

auto InteractiveAnalysis::open_buffer(std::filesystem::path path,
                                      StubLanguage language,
                                      std::string source) -> liquid::result<void> {
    const auto key = path_key(path);
    auto buffer = make_buffer(language, std::move(source));
    if (!buffer) {
        return std::unexpected(buffer.error());
    }
    auto lock = std::scoped_lock(impl_->state->mutex);
    if (impl_->state->buffers.contains(key)) {
        return std::unexpected(failure("interactive buffer is already open", key));
    }
    const auto source_bytes = bytes(*(*buffer)->snapshot->source);
    impl_->state->buffers.emplace(key, std::move(*buffer));
    auto stored = impl_->set_input(overlay_input(key), source_bytes, liquid::durability::volatile_);
    if (!stored) {
        impl_->state->buffers.erase(key);
        return std::unexpected(stored.error());
    }
    auto bumped = impl_->bump_open_buffers();
    if (!bumped) {
        return std::unexpected(bumped.error());
    }
    return {};
}

auto InteractiveAnalysis::apply_edit(const std::filesystem::path& path, SourceEdit edit)
    -> liquid::result<IncrementalParseReport> {
    const auto key = path_key(path);
    auto lock = std::scoped_lock(impl_->state->mutex);
    const auto found = impl_->state->buffers.find(key);
    if (found == impl_->state->buffers.end()) {
        return std::unexpected(failure("interactive buffer is not open", key));
    }
    auto& buffer = *found->second;
    const auto before = buffer.snapshot;
    auto source = edited_source(*before->source, edit);
    if (!source) {
        return std::unexpected(source.error());
    }
    if (source->size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(failure("interactive buffer is too large", key));
    }
    impl_->cancel_queries(key);
    auto invalidated = impl_->graph.mark_changed(overlay_input(key), liquid::durability::volatile_);
    if (!invalidated) {
        return std::unexpected(invalidated.error());
    }

    const auto edit_value = input_edit(*before->source, edit);
    TSTree* edited_old_tree = ts_tree_copy(buffer.tree);
    ts_tree_edit(edited_old_tree, &edit_value);
    TSTree* new_tree = ts_parser_parse_string(
        buffer.parser, edited_old_tree, source->data(), static_cast<std::uint32_t>(source->size()));
    if (new_tree == nullptr) {
        ts_tree_delete(edited_old_tree);
        return std::unexpected(failure("could not incrementally parse buffer", key));
    }
    auto changed = changed_ranges(edited_old_tree, new_tree);
    auto source_owner = std::make_shared<const std::string>(std::move(*source));
    auto after = SnapshotBuilder(source_owner, ts_tree_root_node(new_tree)).build();
    auto report = incremental_report(*after, edit_value, std::move(changed));

    auto stored =
        impl_->set_input(overlay_input(key), bytes(*source_owner), liquid::durability::volatile_);
    if (!stored) {
        ts_tree_delete(new_tree);
        ts_tree_delete(edited_old_tree);
        return std::unexpected(stored.error());
    }
    ts_tree_delete(buffer.tree);
    buffer.tree = new_tree;
    buffer.snapshot = std::move(after);
    ts_tree_delete(edited_old_tree);
    auto bumped = impl_->bump_open_buffers();
    if (!bumped) {
        return std::unexpected(bumped.error());
    }
    return report;
}

auto InteractiveAnalysis::close_buffer(const std::filesystem::path& path) -> liquid::result<void> {
    const auto key = path_key(path);
    auto lock = std::scoped_lock(impl_->state->mutex);
    if (!impl_->state->buffers.contains(key)) {
        return std::unexpected(failure("interactive buffer is not open", key));
    }
    impl_->cancel_queries(key);
    auto cleared = liquid::runtime::block_on(
        impl_->graph.clear_input(overlay_input(key), liquid::durability::volatile_));
    if (!cleared) {
        return std::unexpected(cleared.error());
    }
    impl_->state->buffers.erase(key);
    return impl_->bump_open_buffers();
}

auto InteractiveAnalysis::add_stub(std::filesystem::path path, StubTree tree)
    -> liquid::result<void> {
    const auto key = path_key(path);
    auto lock = std::scoped_lock(impl_->state->mutex);
    auto before = impl_->index;
    const auto found = std::ranges::find(
        impl_->index.stubs, key, &decltype(impl_->index.stubs)::value_type::first);
    if (found == impl_->index.stubs.end()) {
        impl_->index.stubs.emplace_back(key, std::move(tree));
    } else {
        found->second = std::move(tree);
    }
    auto stored = impl_->set_input(
        analysis_index_input(), encode_analysis_index(impl_->index), liquid::durability::low);
    if (!stored) {
        impl_->index = std::move(before);
        return std::unexpected(stored.error());
    }
    return {};
}

auto InteractiveAnalysis::set_symbol_records(std::vector<SymbolRecord> records)
    -> liquid::result<void> {
    auto lock = std::scoped_lock(impl_->state->mutex);
    auto before = impl_->index.symbols;
    impl_->index.symbols = std::move(records);
    auto stored = impl_->set_input(
        analysis_index_input(), encode_analysis_index(impl_->index), liquid::durability::low);
    if (!stored) {
        impl_->index.symbols = std::move(before);
        return std::unexpected(stored.error());
    }
    return {};
}

auto InteractiveAnalysis::set_build_diagnostics(std::filesystem::path buffer_path,
                                                std::filesystem::path translation_unit,
                                                std::vector<Diagnostic> diagnostics)
    -> liquid::result<void> {
    const auto buffer_key = path_key(buffer_path);
    const auto translation_unit_key = path_key(translation_unit);
    auto lock = std::scoped_lock(impl_->state->mutex);
    auto stored = impl_->set_input(diagnostics_input(translation_unit_key),
                                   encode_diagnostics(diagnostics),
                                   liquid::durability::low);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    const auto previous = impl_->translation_units.find(buffer_key);
    auto previous_value = std::optional<std::string>{};
    if (previous != impl_->translation_units.end()) {
        previous_value = previous->second;
    }
    impl_->translation_units.insert_or_assign(buffer_key, translation_unit_key);
    stored = impl_->set_input(translation_units_input(),
                              encode_translation_units(impl_->translation_units),
                              liquid::durability::low);
    if (!stored) {
        if (previous_value) {
            impl_->translation_units.insert_or_assign(buffer_key, *previous_value);
        } else {
            impl_->translation_units.erase(buffer_key);
        }
        return std::unexpected(stored.error());
    }
    return {};
}

auto InteractiveAnalysis::node_at_position(const std::filesystem::path& path, std::uint32_t byte)
    -> liquid::result<std::optional<SyntaxNodeInfo>> {
    auto value = impl_->demand(InteractiveQueryKind::node_at_position,
                               encode_cursor_argument(path_key(path), byte));
    if (!value) {
        return std::unexpected(value.error());
    }
    return decode_syntax_node(*value);
}

auto InteractiveAnalysis::enclosing_definition(const std::filesystem::path& path,
                                               std::uint32_t byte)
    -> liquid::result<std::optional<SyntaxNodeInfo>> {
    auto value = impl_->demand(InteractiveQueryKind::enclosing_definition,
                               encode_cursor_argument(path_key(path), byte));
    if (!value) {
        return std::unexpected(value.error());
    }
    return decode_syntax_node(*value);
}

auto InteractiveAnalysis::go_to_definition(const std::filesystem::path& path, std::uint32_t byte)
    -> liquid::result<std::optional<SymbolLocation>> {
    auto value = impl_->demand(InteractiveQueryKind::go_to_definition,
                               encode_cursor_argument(path_key(path), byte));
    if (!value) {
        return std::unexpected(value.error());
    }
    return decode_optional_symbol(*value);
}

auto InteractiveAnalysis::find_references(const std::filesystem::path& path, std::uint32_t byte)
    -> liquid::result<std::vector<ReferenceLocation>> {
    auto value = impl_->demand(InteractiveQueryKind::find_references,
                               encode_cursor_argument(path_key(path), byte));
    if (!value) {
        return std::unexpected(value.error());
    }
    return decode_references(*value);
}

auto InteractiveAnalysis::hover(const std::filesystem::path& path, std::uint32_t byte)
    -> liquid::result<std::optional<HoverInfo>> {
    auto value =
        impl_->demand(InteractiveQueryKind::hover, encode_cursor_argument(path_key(path), byte));
    if (!value) {
        return std::unexpected(value.error());
    }
    return decode_hover(*value);
}

auto InteractiveAnalysis::completion(const std::filesystem::path& path, std::uint32_t byte)
    -> liquid::result<std::vector<CompletionCandidate>> {
    auto value = impl_->demand(InteractiveQueryKind::completion,
                               encode_cursor_argument(path_key(path), byte));
    if (!value) {
        return std::unexpected(value.error());
    }
    return decode_completions(*value);
}

auto InteractiveAnalysis::diagnostics(const std::filesystem::path& path)
    -> liquid::result<std::vector<Diagnostic>> {
    auto value =
        impl_->demand(InteractiveQueryKind::diagnostics, encode_path_argument(path_key(path)));
    if (!value) {
        return std::unexpected(value.error());
    }
    return decode_diagnostics(*value);
}

auto InteractiveAnalysis::definition_context(const std::filesystem::path& path, std::uint32_t byte)
    -> liquid::result<DefinitionContext> {
    auto output = DefinitionContext{};
    auto enclosing = enclosing_definition(path, byte);
    if (!enclosing) {
        return std::unexpected(enclosing.error());
    }
    output.enclosing_definition = std::move(*enclosing);
    if (!output.enclosing_definition) {
        return output;
    }

    auto current = std::shared_ptr<const BufferSnapshot>{};
    auto snapshots = std::vector<std::pair<std::string, std::shared_ptr<const BufferSnapshot>>>{};
    auto index = AnalysisIndex{};
    {
        auto lock = std::scoped_lock(impl_->state->mutex);
        auto found = impl_->state->buffers.find(path_key(path));
        if (found == impl_->state->buffers.end()) {
            return std::unexpected(failure("interactive buffer is not open", path_key(path)));
        }
        current = found->second->snapshot;
        snapshots.reserve(impl_->state->buffers.size());
        for (const auto& [buffer_path, buffer] : impl_->state->buffers) {
            snapshots.emplace_back(buffer_path, buffer->snapshot);
        }
        index = impl_->index;
    }

    const auto& enclosing_range = output.enclosing_definition->range;
    const DeclarationEntry* enclosing_declaration = nullptr;
    for (const auto& entry : current->declarations) {
        if (entry.declaration.range.start_byte != enclosing_range.start_byte ||
            entry.declaration.range.end_byte != enclosing_range.end_byte) {
            continue;
        }
        enclosing_declaration = &entry;
        break;
    }
    auto enclosing_name = std::string{};
    if (enclosing_declaration != nullptr) {
        enclosing_name = enclosing_declaration->declaration.name;
        if (const auto* symbol = find_symbol_record(index, enclosing_name); symbol != nullptr) {
            output.enclosing_symbol = *symbol;
        } else {
            auto synthesized = SymbolRecord{};
            synthesized.name = enclosing_name;
            synthesized.definition = SymbolLocation{path, enclosing_declaration->declaration};
            const auto references =
                collect_references(index, snapshots, path_key(path), enclosing_name);
            for (const auto& reference : references) {
                if (!reference.is_definition) {
                    synthesized.references.push_back(reference.location);
                }
            }
            output.enclosing_symbol = std::move(synthesized);
        }
    }

    auto seen = std::set<std::string>{};
    for (const auto& identifier : current->identifiers) {
        if (!ranges_intersect(identifier.range, enclosing_range) ||
            identifier.name == enclosing_name || !seen.insert(identifier.name).second) {
            continue;
        }
        auto definition = find_definition(index, snapshots, path_key(path), identifier.name);
        if (!definition) {
            continue;
        }
        output.referenced_definitions.push_back(
            {identifier.name, definition->declaration.signature, std::move(*definition)});
    }
    return output;
}

auto InteractiveAnalysis::measure_latency(const std::filesystem::path& path,
                                          std::span<const std::uint32_t> positions)
    -> liquid::result<InteractiveLatency> {
    if (positions.empty()) {
        return std::unexpected(failure("latency measurement needs positions"));
    }
    auto definition_samples = std::vector<double>{};
    auto completion_samples = std::vector<double>{};
    definition_samples.reserve(positions.size());
    completion_samples.reserve(positions.size());
    for (const auto position : positions) {
        auto started = std::chrono::steady_clock::now();
        auto definition = go_to_definition(path, position);
        if (!definition) {
            return std::unexpected(definition.error());
        }
        auto finished = std::chrono::steady_clock::now();
        definition_samples.push_back(
            std::chrono::duration<double, std::milli>(finished - started).count());

        started = std::chrono::steady_clock::now();
        auto candidates = completion(path, position);
        if (!candidates) {
            return std::unexpected(candidates.error());
        }
        finished = std::chrono::steady_clock::now();
        completion_samples.push_back(
            std::chrono::duration<double, std::milli>(finished - started).count());
    }
    return InteractiveLatency{static_cast<std::uint32_t>(positions.size()),
                              percentile_95(std::move(definition_samples)),
                              percentile_95(std::move(completion_samples))};
}

auto InteractiveAnalysis::flush() -> liquid::result<void> {
    auto flushed = liquid::runtime::block_on(impl_->graph.flush());
    if (!flushed) {
        return std::unexpected(flushed.error());
    }
    return {};
}

auto InteractiveAnalysis::statistics() const -> liquid::graph_statistics {
    return impl_->graph.statistics();
}

auto interactive_query_kind_name(InteractiveQueryKind kind) -> std::string_view {
    switch (kind) {
    case InteractiveQueryKind::node_at_position:
        return "liquify.node-at-position/1";
    case InteractiveQueryKind::enclosing_definition:
        return "liquify.enclosing-definition/1";
    case InteractiveQueryKind::go_to_definition:
        return "liquify.go-to-definition/1";
    case InteractiveQueryKind::find_references:
        return "liquify.find-references/1";
    case InteractiveQueryKind::hover:
        return "liquify.hover/1";
    case InteractiveQueryKind::completion:
        return "liquify.completion/1";
    case InteractiveQueryKind::diagnostics:
        return "liquify.diagnostics/1";
    }
    return "liquify.unknown/1";
}

auto diagnostic_severity_name(DiagnosticSeverity severity) -> std::string_view {
    switch (severity) {
    case DiagnosticSeverity::note:
        return "note";
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::error:
        return "error";
    }
    return "unknown";
}

} // namespace liquify
