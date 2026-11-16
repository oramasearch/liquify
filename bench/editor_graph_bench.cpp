// SPDX-License-Identifier: MIT

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../src/repository_view.hpp"
#include "liquid/git.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/editor_graph.hpp"

namespace {

auto fail(const liquid::error& error) -> int {
    std::println(stderr, "editor graph measurement: {}: {}", error.context, error.message);
    return 1;
}

auto source_files(const std::filesystem::path& root) -> std::uint64_t {
    auto count = std::uint64_t{};
    auto error = std::error_code{};
    for (auto iterator =
             std::filesystem::recursive_directory_iterator{
                 root, std::filesystem::directory_options::skip_permission_denied, error};
         !error && iterator != std::filesystem::recursive_directory_iterator{};
         iterator.increment(error)) {
        if (iterator->is_directory(error)) {
            const auto name = iterator->path().filename().string();
            if (name == ".git" || name == ".liquid" || name == "modules" ||
                name.starts_with("build")) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (iterator->is_regular_file(error)) {
            ++count;
        }
    }
    return count;
}

auto git_source(const std::filesystem::path& checkout) -> std::filesystem::path {
    const auto marker = checkout / ".git";
    if (std::filesystem::is_directory(marker)) {
        return checkout;
    }
    auto input = std::ifstream{marker};
    auto line = std::string{};
    std::getline(input, line);
    constexpr auto prefix = std::string_view{"gitdir: "};
    if (!line.starts_with(prefix)) {
        return checkout;
    }
    auto directory = std::filesystem::path{line.substr(prefix.size())};
    if (directory.is_relative()) {
        directory = checkout / directory;
    }
    return std::filesystem::weakly_canonical(directory);
}

auto prepare_repository(const std::filesystem::path& source,
                        const std::filesystem::path& destination) -> liquid::result<void> {
    if (std::filesystem::exists(destination / "LIQUID_REPO")) {
        return {};
    }
    auto created = liquid::runtime::block_on(liquid::repository::create(destination));
    if (!created) {
        return std::unexpected(created.error());
    }
    auto imported = liquid::runtime::block_on(liquid::import_git(*created, git_source(source)));
    if (!imported) {
        return std::unexpected(imported.error());
    }
    return {};
}

auto text(std::span<const std::byte> value) -> std::string {
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr,
                     "usage: liquify-editor-graph-bench <liquid-checkout> <repository-state>");
        return 2;
    }
    const auto liquid_checkout = std::filesystem::absolute(argv[1]);
    const auto repository_state = std::filesystem::absolute(argv[2]);
    auto prepared = prepare_repository(liquid_checkout, repository_state);
    if (!prepared) {
        return fail(prepared.error());
    }

    const auto open_started = std::chrono::steady_clock::now();
    auto view = liquify::RepositoryView::open(repository_state);
    const auto open_finished = std::chrono::steady_clock::now();
    if (!view.error().empty()) {
        std::println(stderr, "editor graph measurement: {}", view.error());
        return 1;
    }

    auto repository = liquid::runtime::block_on(liquid::repository::open(repository_state));
    if (!repository) {
        return fail(repository.error());
    }
    auto source_value = liquid::runtime::block_on(repository->read("src/store/faults.cpp"));
    if (!source_value) {
        return fail(source_value.error());
    }
    auto source = text(*source_value);

    auto graph = liquify::EditorGraph(repository_state);
    graph.open_buffer("src/store/faults.cpp", liquify::StubLanguage::cpp, source);
    graph.wait_until_idle();

    auto link_release = std::promise<void>{};
    auto link_started = std::promise<void>{};
    auto link_started_future = link_started.get_future();
    auto link = std::async(
        std::launch::async,
        [release = link_release.get_future(), started = std::move(link_started)]() mutable {
            started.set_value();
            release.wait();
        });
    link_started_future.wait();

    const auto inserted = std::string{"// editor diagnostic measurement\n"};
    graph.apply_edit("src/store/faults.cpp", {0, 0, inserted});
    graph.publish_build_diagnostics("src/store/faults.cpp",
                                    "src/store/faults.cpp",
                                    {{{"src/store/faults.cpp", {0, 2, 0, 0, 0, 2}},
                                      liquify::DiagnosticSeverity::warning,
                                      "measurement diagnostic"}});
    graph.request_diagnostics("src/store/faults.cpp");
    graph.wait_until_idle();
    const auto snapshot = graph.snapshot();
    const auto before_relink =
        link.wait_for(std::chrono::milliseconds::zero()) != std::future_status::ready;
    link_release.set_value();
    link.get();

    const auto open_ms =
        std::chrono::duration<double, std::milli>(open_finished - open_started).count();
    std::println("editor graph: liquid_files={} root_entries={} repository_open_ms={:.3f} "
                 "root_read_ms={:.3f} edit_to_diagnostic_ms={:.3f} "
                 "diagnostic_before_relink={}",
                 source_files(liquid_checkout),
                 view.entries().size(),
                 open_ms,
                 view.open_time().count(),
                 snapshot.edit_to_diagnostic_ms,
                 before_relink);
    return snapshot.diagnostics.empty() || !before_relink ? 1 : 0;
}
