#include "liquify/measure.hpp"

#include <chrono>
#include <fstream>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/editor_graph.hpp"

namespace liquify {
namespace {

auto bytes(std::string_view value) -> std::vector<std::byte> {
    const auto characters = std::span(value.data(), value.size());
    const auto source = std::as_bytes(characters);
    return {source.begin(), source.end()};
}

auto text(std::span<const std::byte> value) -> std::string {
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

auto load_average() -> std::string {
    auto source = std::ifstream{"/proc/loadavg"};
    auto output = std::string{};
    std::getline(source, output);
    return output.empty() ? "unavailable" : output;
}

auto json_string(std::string_view value) -> std::string {
    auto output = std::string{"\""};
    for (const auto character : value) {
        if (character == '\\' || character == '\"') {
            output.push_back('\\');
        }
        if (character == '\n') {
            output += "\\n";
        } else {
            output.push_back(character);
        }
    }
    output.push_back('\"');
    return output;
}

auto fail(const liquid::error& error) -> int {
    std::println(stderr, "liquify: {}: {}", error.context, error.message);
    return 1;
}

} // namespace

auto measure_build(const std::filesystem::path& repository_path, std::string target) -> int {
    auto graph = EditorGraph(repository_path);
    graph.wait_until_idle();
    const auto ready = graph.snapshot();
    if (!ready.ready) {
        std::println(stderr, "liquify: {}", ready.error);
        return 1;
    }
    auto repository = liquid::runtime::block_on(liquid::repository::open_build(repository_path));
    if (!repository) {
        return fail(repository.error());
    }
    auto source = liquid::runtime::block_on(repository->read("src/store/faults.cpp"));
    if (!source) {
        return fail(source.error());
    }
    auto edited = text(*source);
    constexpr auto marker = std::string_view{"injector->visit(event);"};
    const auto position = edited.find(marker);
    if (position == std::string::npos) {
        std::println(stderr, "liquify: measurement marker is absent from src/store/faults.cpp");
        return 1;
    }
    edited.insert(position + marker.size(), " /* liquify measurement */");
    const auto saved_at = std::chrono::steady_clock::now();
    auto saved = liquid::runtime::block_on(repository->write("src/store/faults.cpp", bytes(edited)));
    if (!saved) {
        return fail(saved.error());
    }
    auto change = liquid::runtime::block_on(repository->cut("liquify build measurement"));
    if (!change) {
        return fail(change.error());
    }

    const auto build_requested = std::chrono::steady_clock::now();
    graph.request_build({std::move(target)});
    graph.wait_until_idle();
    const auto build = graph.snapshot();
    if (!build.query_error.empty()) {
        std::println(stderr, "liquify: {}", build.query_error);
        return 1;
    }
    if (!build.build_result.starts_with("passed:")) {
        std::println(stderr, "liquify: build did not pass: {}", build.build_result);
        return 1;
    }
    const auto saved_to_start =
        std::chrono::duration_cast<std::chrono::milliseconds>(build_requested - saved_at) +
        build.build_first_action;
    const auto saved_to_result = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - saved_at);
    std::println("{{\"type\":\"build\",\"save_to_first_action_ms\":{},\"save_to_build_result_ms\":{},\"load_average\":{}}}",
                 saved_to_start.count(),
                 saved_to_result.count(),
                 json_string(load_average()));

    graph.refresh_tests();
    graph.wait_until_idle();
    const auto tests = graph.snapshot();
    if (tests.tests.empty()) {
        std::println(stderr, "liquify: configured build has no test targets");
        return 1;
    }
    const auto test_name = tests.tests.front().name;
    const auto test_started = std::chrono::steady_clock::now();
    graph.request_tests(test_name);
    graph.wait_until_idle();
    const auto completed = graph.snapshot();
    if (!completed.query_error.empty()) {
        std::println(stderr, "liquify: {}", completed.query_error);
        return 1;
    }
    const auto test = std::ranges::find(completed.test_results, test_name, &TestSnapshot::name);
    if (test == completed.test_results.end() || !test->passed) {
        std::println(stderr, "liquify: test did not pass: {}", test_name);
        return 1;
    }
    const auto test_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - test_started);
    std::println("{{\"type\":\"test\",\"name\":{},\"end_to_end_ms\":{},\"load_average\":{}}}",
                 json_string(test_name),
                 test_elapsed.count(),
                 json_string(load_average()));
    std::println("{{\"type\":\"system\",\"load_average\":{}}}",
                 json_string(load_average()));
    return 0;
}

} // namespace liquify
