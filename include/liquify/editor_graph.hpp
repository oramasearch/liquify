#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "liquid/build.hpp"
#include "liquid/daemon.hpp"
#include "liquid/graph.hpp"
#include "liquid/sync.hpp"
#include "liquify/interactive_analysis.hpp"

namespace liquify {

enum class BuildActionState : std::uint8_t {
    queued,
    running_local,
    running_remote,
    hit,
    done,
    failed,
};

struct BuildActionSnapshot {
    std::string identity;
    BuildActionState state{BuildActionState::queued};
    std::string worker;
    std::string scheduling_choice;
    bool critical{};
    std::vector<Diagnostic> diagnostics;
};

struct TestSnapshot {
    std::string name;
    bool passed{};
    bool hit{};
    std::string output;
    std::chrono::milliseconds duration{};
};

struct DeploySnapshot {
    bool running{};
    std::string error;
    std::uint64_t objects{};
    std::uint64_t bytes{};
    std::vector<liquid::sync_ref_outcome> references;
};

struct EditorGraphSnapshot {
    bool ready{};
    bool daemon_running{};
    bool daemon_hosted{};
    std::string error;
    std::string query_error;
    std::optional<liquid::daemon_status> daemon;
    liquid::graph_statistics graph;
    std::uint64_t repository_pending_drips{};
    std::vector<Diagnostic> diagnostics;
    std::optional<HoverInfo> hover;
    std::vector<CompletionCandidate> completions;
    std::optional<SymbolLocation> definition;
    std::uint64_t diagnostics_generation{};
    std::uint64_t hover_generation{};
    std::uint64_t completion_generation{};
    std::uint64_t definition_generation{};
    double edit_to_diagnostic_ms{};
    bool build_running{};
    std::string build_result;
    std::vector<BuildActionSnapshot> build_actions;
    std::vector<liquid::critical_action> critical_path;
    std::vector<liquid::test_target> tests;
    std::vector<TestSnapshot> test_results;
    std::vector<Diagnostic> build_diagnostics;
    DeploySnapshot deploy;
};

class EditorGraph {
public:
    explicit EditorGraph(std::filesystem::path repository_path);
    ~EditorGraph();
    EditorGraph(const EditorGraph&) = delete;
    auto operator=(const EditorGraph&) -> EditorGraph& = delete;
    EditorGraph(EditorGraph&&) = delete;
    auto operator=(EditorGraph&&) -> EditorGraph& = delete;

    void open_buffer(std::filesystem::path path, StubLanguage language, std::string source);
    void apply_edit(std::filesystem::path path, SourceEdit edit);
    void close_buffer(std::filesystem::path path);
    void publish_build_diagnostics(std::filesystem::path buffer_path,
                                   std::filesystem::path translation_unit,
                                   std::vector<Diagnostic> diagnostics);

    void request_diagnostics(std::filesystem::path path);
    void request_hover(std::filesystem::path path, std::uint32_t byte);
    void request_completion(std::filesystem::path path, std::uint32_t byte);
    void request_definition(std::filesystem::path path, std::uint32_t byte);
    void request_build(liquid::build_target target = {});
    void request_tests(std::string filter = {});
    void refresh_tests();
    void request_deploy();
    void publish_build_event(liquid::build_event event);
    void publish_deploy_report(liquid::sync_report report);
    void note_saved_drip(std::uint64_t repository_pending_drips);
    void refresh_status();

    [[nodiscard]] auto snapshot() const -> EditorGraphSnapshot;
    void wait_until_idle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] auto editor_graph_status(const EditorGraphSnapshot& snapshot) -> std::string;

} // namespace liquify
