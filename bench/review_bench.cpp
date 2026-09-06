#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/review.hpp"

namespace {

auto bytes(std::string_view value) -> std::vector<std::byte> {
    const auto source = std::as_bytes(std::span(value.data(), value.size()));
    return {source.begin(), source.end()};
}

auto read_file(const std::filesystem::path& path) -> std::string {
    auto input = std::ifstream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

auto liquid_files(const std::filesystem::path& root) -> std::vector<std::filesystem::path> {
    auto output = std::vector<std::filesystem::path>{};
    for (auto iterator =
             std::filesystem::recursive_directory_iterator{
                 root, std::filesystem::directory_options::skip_permission_denied};
         iterator != std::filesystem::recursive_directory_iterator{};
         ++iterator) {
        if (iterator->is_directory()) {
            const auto name = iterator->path().filename().string();
            if (name == ".git" || name == ".liquid" || name == "build") {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (!iterator->is_regular_file()) {
            continue;
        }
        const auto extension = iterator->path().extension().string();
        if (extension == ".cpp" || extension == ".hpp" || extension == ".md") {
            output.push_back(iterator->path());
        }
    }
    std::ranges::sort(output);
    if (output.size() > 50) {
        output.resize(50);
    }
    return output;
}

auto fail(const liquid::error& error) -> int {
    std::cerr << error.message << " (" << error.context << ")\n";
    return 1;
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 3) {
        std::cerr << "usage: liquify-review-bench <liquid-tree> <repository>\n";
        return 2;
    }
    const auto source_root = std::filesystem::path{argv[1]};
    const auto repository_path = std::filesystem::path{argv[2]};
    const auto files = liquid_files(source_root);
    if (files.size() != 50) {
        std::cerr << "liquid tree did not contain 50 eligible files\n";
        return 1;
    }
    auto repository = liquid::runtime::block_on(liquid::repository::create(repository_path));
    if (!repository) {
        return fail(repository.error());
    }
    auto written =
        liquid::runtime::block_on(repository->write("measurement-base.txt", bytes("base\n")));
    if (!written) {
        return fail(written.error());
    }
    auto base = liquid::runtime::block_on(repository->cut("measurement base"));
    if (!base) {
        return fail(base.error());
    }
    for (const auto& file : files) {
        const auto relative = std::filesystem::relative(file, source_root).generic_string();
        const auto contents = read_file(file);
        written =
            liquid::runtime::block_on(repository->write("liquid/" + relative, bytes(contents)));
        if (!written) {
            return fail(written.error());
        }
    }
    auto head = liquid::runtime::block_on(repository->cut("50-file review"));
    if (!head) {
        return fail(head.error());
    }
    const auto opened_at = std::chrono::steady_clock::now();
    auto diff = liquid::runtime::block_on(liquify::build_review_diff(*repository, *base, *head));
    const auto open_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - opened_at)
            .count();
    if (!diff) {
        return fail(diff.error());
    }

    const auto anchor_source_path = source_root / "src/repository/repository_impl.cpp";
    auto anchor_source = read_file(anchor_source_path);
    written = liquid::runtime::block_on(repository->write("anchor.cpp", bytes(anchor_source)));
    if (!written) {
        return fail(written.error());
    }
    auto anchor_base = liquid::runtime::block_on(repository->cut("200 anchor source"));
    if (!anchor_base) {
        return fail(anchor_base.error());
    }
    auto anchors = std::vector<liquid::text_anchor>{};
    anchors.reserve(200);
    for (auto line = std::uint64_t{1}; line <= 200; ++line) {
        anchors.push_back(
            {.change = *anchor_base, .path = "anchor.cpp", .line = line, .column = 1});
    }
    anchor_source.insert(0, "// shifted by review benchmark\n");
    written = liquid::runtime::block_on(repository->write("anchor.cpp", bytes(anchor_source)));
    if (!written) {
        return fail(written.error());
    }
    auto anchor_head = liquid::runtime::block_on(repository->cut("200 anchor target"));
    if (!anchor_head) {
        return fail(anchor_head.error());
    }
    const auto mapped_at = std::chrono::steady_clock::now();
    auto mapped = liquid::runtime::block_on(repository->map_anchors(anchors, *anchor_head));
    const auto mapping_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mapped_at)
            .count();
    if (!mapped) {
        return fail(mapped.error());
    }
    const auto moved =
        std::ranges::count_if(*mapped, [](const auto& anchor) { return anchor.moved; });
    const auto approximate =
        std::ranges::count_if(*mapped, [](const auto& anchor) { return anchor.approximate; });
    const auto deleted =
        std::ranges::count_if(*mapped, [](const auto& anchor) { return anchor.deleted; });
    std::cout << "review: liquid_files=" << files.size() << " diff_lines=" << diff->size()
              << " open_to_first_hunk_ms=" << open_ms << " comments=" << mapped->size()
              << " anchor_mapping_ms=" << mapping_ms << " moved=" << moved
              << " approximate=" << approximate << " deleted=" << deleted << '\n';
    return mapped->size() == 200 ? 0 : 1;
}
