// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "liquify/interactive_analysis.hpp"

namespace {

auto source_files(const std::filesystem::path& root) -> std::vector<std::filesystem::path> {
    auto output = std::vector<std::filesystem::path>{};
    auto error = std::error_code{};
    auto iterator = std::filesystem::recursive_directory_iterator{
        root, std::filesystem::directory_options::skip_permission_denied, error};
    for (auto end = std::filesystem::recursive_directory_iterator{}; iterator != end;
         iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (iterator->is_directory() &&
            (iterator->path().filename() == ".git" || iterator->path().filename() == "build" ||
             iterator->path().filename() == "modules")) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (iterator->is_regular_file() && liquify::stub_language_for_path(iterator->path())) {
            output.push_back(iterator->path());
        }
    }
    std::ranges::sort(output);
    return output;
}

auto read_file(const std::filesystem::path& path) -> std::string {
    auto input = std::ifstream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

auto identifier_positions(std::string_view source, const std::set<std::string>& declarations)
    -> std::vector<std::uint32_t> {
    auto output = std::vector<std::uint32_t>{};
    auto cursor = std::size_t{};
    while (cursor < source.size()) {
        if (!std::isalpha(static_cast<unsigned char>(source[cursor])) && source[cursor] != '_') {
            ++cursor;
            continue;
        }
        const auto begin = cursor++;
        while (
            cursor < source.size() &&
            (std::isalnum(static_cast<unsigned char>(source[cursor])) || source[cursor] == '_')) {
            ++cursor;
        }
        if (declarations.contains(std::string(source.substr(begin, cursor - begin)))) {
            output.push_back(static_cast<std::uint32_t>(begin));
            if (output.size() == 200) {
                break;
            }
        }
    }
    return output;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: liquify-interactive-analysis-bench <source-root> "
                     "<state-directory>\n";
        return 2;
    }
    const auto root = std::filesystem::absolute(argv[1]);
    const auto state = std::filesystem::absolute(argv[2]);
    const auto files = source_files(root);
    if (files.empty()) {
        std::cerr << "no C or C++ sources found\n";
        return 1;
    }

    auto target = root / "src/graph.cpp";
    if (!std::filesystem::is_regular_file(target)) {
        target = files.front();
    }
    auto stubs = liquify::StubTrees::open(state / "stubs");
    if (!stubs) {
        std::cerr << stubs.error().message << '\n';
        return 1;
    }
    auto analysis = liquify::InteractiveAnalysis::open(state / "interactive");
    if (!analysis) {
        std::cerr << analysis.error().message << '\n';
        return 1;
    }

    auto declaration_names = std::set<std::string>{};
    for (const auto& path : files) {
        auto tree = stubs->for_file(path);
        if (!tree) {
            std::cerr << path << ": " << tree.error().message << '\n';
            return 1;
        }
        for (const auto& declaration : tree->declarations) {
            declaration_names.insert(declaration.name);
        }
        if (path != target) {
            auto added =
                analysis->add_stub(std::filesystem::relative(path, root), std::move(*tree));
            if (!added) {
                std::cerr << added.error().message << '\n';
                return 1;
            }
        }
    }

    const auto source = read_file(target);
    auto language = liquify::stub_language_for_path(target);
    if (!language) {
        std::cerr << language.error().message << '\n';
        return 1;
    }
    const auto relative_target = std::filesystem::relative(target, root);
    auto opened = analysis->open_buffer(relative_target, *language, source);
    if (!opened) {
        std::cerr << opened.error().message << '\n';
        return 1;
    }
    const auto positions = identifier_positions(source, declaration_names);
    if (positions.empty()) {
        std::cerr << "no declaration references found in target\n";
        return 1;
    }
    auto latency = analysis->measure_latency(relative_target, positions);
    if (!latency) {
        std::cerr << latency.error().message << '\n';
        return 1;
    }
    std::cout << "interactive analysis: files=" << files.size()
              << " open_buffers=1 samples=" << latency->samples
              << " go_to_definition_p95_ms=" << latency->go_to_definition_p95_ms
              << " completion_p95_ms=" << latency->completion_p95_ms << '\n';
    return 0;
}
