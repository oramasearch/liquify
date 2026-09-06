#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "liquify/context_builder.hpp"
#include "liquify/interactive_analysis.hpp"
#include "liquify/stub_tree.hpp"

namespace {

auto read_file(const std::filesystem::path& path) -> std::string {
    auto input = std::ifstream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

auto source_files(const std::filesystem::path& root) -> std::vector<std::filesystem::path> {
    auto output = std::vector<std::filesystem::path>{};
    auto error = std::error_code{};
    auto iterator = std::filesystem::recursive_directory_iterator{
        root, std::filesystem::directory_options::skip_permission_denied, error};
    const auto end = std::filesystem::recursive_directory_iterator{};
    while (iterator != end) {
        if (error) {
            error.clear();
            iterator.increment(error);
            continue;
        }
        if (iterator->is_directory() &&
            (iterator->path().filename() == ".git" || iterator->path().filename() == "build" ||
             iterator->path().filename() == "modules")) {
            iterator.disable_recursion_pending();
        } else if (iterator->is_regular_file() &&
                   liquify::stub_language_for_path(iterator->path())) {
            output.push_back(iterator->path());
        }
        iterator.increment(error);
    }
    std::ranges::sort(output);
    return output;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: liquify-ai-context-bench <liquid-root> <state-directory>\n";
        return 2;
    }
    const auto root = std::filesystem::absolute(argv[1]);
    const auto state = std::filesystem::absolute(argv[2]);
    const auto target = root / "src/repository/repository_impl.cpp";
    if (!std::filesystem::is_regular_file(target)) {
        std::cerr << "liquid repository implementation source is missing\n";
        return 1;
    }
    auto stubs = liquify::StubTrees::open(state / "stubs");
    auto analysis = liquify::InteractiveAnalysis::open(state / "analysis");
    if (!stubs || !analysis) {
        std::cerr << (!stubs ? stubs.error().message : analysis.error().message) << '\n';
        return 1;
    }
    auto target_tree = liquify::StubTree{};
    for (const auto& path : source_files(root)) {
        auto tree = stubs->for_file(path);
        if (!tree) {
            std::cerr << tree.error().message << '\n';
            return 1;
        }
        if (path == target) {
            target_tree = *tree;
            continue;
        }
        auto added = analysis->add_stub(std::filesystem::relative(path, root), std::move(*tree));
        if (!added) {
            std::cerr << added.error().message << '\n';
            return 1;
        }
    }
    const auto source = read_file(target);
    const auto relative = std::filesystem::relative(target, root);
    auto opened = analysis->open_buffer(relative, target_tree.language, source);
    if (!opened) {
        std::cerr << opened.error().message << '\n';
        return 1;
    }
    auto selected = liquify::DefinitionContext{};
    auto selected_declaration = liquify::StubDeclaration{};
    for (const auto& declaration : target_tree.declarations) {
        if (declaration.kind != liquify::DeclarationKind::function) {
            continue;
        }
        const auto byte = declaration.range.start_byte +
                          (declaration.range.end_byte - declaration.range.start_byte) / 2;
        auto candidate = analysis->definition_context(relative, byte);
        if (candidate &&
            candidate->referenced_definitions.size() > selected.referenced_definitions.size()) {
            selected = std::move(*candidate);
            selected_declaration = declaration;
        }
    }
    if (!selected.enclosing_definition || !selected.enclosing_symbol ||
        selected.referenced_definitions.size() < 20) {
        std::cerr << "no definition with 20 resolved references; maximum="
                  << selected.referenced_definitions.size() << '\n';
        return 1;
    }
    auto facts = liquify::ContextFacts{};
    facts.path = relative;
    facts.focus = selected.enclosing_definition->range;
    facts.enclosing_definition = selected.enclosing_definition;
    facts.enclosing_symbol =
        liquify::SymbolRecord{selected_declaration.name, {relative, selected_declaration}, {}};
    facts.whole_file_bytes = source.size();
    for (auto index = std::size_t{}; index < 20; ++index) {
        const auto& reference = selected.referenced_definitions[index];
        facts.referenced_definitions.push_back(
            {reference.name,
             reference.signature,
             {reference.definition.path, reference.definition.declaration.range}});
    }
    auto block = liquify::ContextBlock{};
    const auto started = std::chrono::steady_clock::now();
    for (auto iteration = 0; iteration < 1'000; ++iteration) {
        block = liquify::ContextBuilder(64 * 1024).build(facts);
    }
    const auto elapsed =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started)
            .count() /
        1'000.0;
    const auto percentage = source.empty() ? 0.0
                                           : 100.0 * static_cast<double>(block.bytes) /
                                                 static_cast<double>(source.size());
    std::cout << "ai context: definition=" << selected_declaration.name
              << " references=20 bytes=" << block.bytes << " whole_file_bytes=" << source.size()
              << " percent_of_whole_file=" << percentage << " assembly_us=" << elapsed << '\n';
    return 0;
}
