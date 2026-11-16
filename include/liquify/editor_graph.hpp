#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "liquid/build.hpp"
#include "liquid/daemon.hpp"
#include "liquid/drive.hpp"
#include "liquid/graph.hpp"
#include "liquid/repository.hpp"
#include "liquid/session.hpp"
#include "liquid/sync.hpp"
#include "liquify/context_builder.hpp"
#include "liquify/editor_model.hpp"
#include "liquify/interactive_analysis.hpp"
#include "liquify/structural_edit.hpp"

namespace liquify {

// Where the editor keeps a repository's analysis cache (liquify#79): a per-user directory keyed by
// the repository's canonical path, beside liquify's other per-user state, never inside the
// repository, so a repository cannot ship a crafted cache. `state_root` defaults to the directory of
// default_first_run_state_path().
[[nodiscard]] auto analysis_cache_directory(const std::filesystem::path &repository_path,
                                            std::optional<std::filesystem::path> state_root =
                                                std::nullopt) -> std::filesystem::path;

class PluginHost;

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
  std::vector<std::string> dependencies;
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
  // liquid's error for the failed push, and the sync remote it went to (empty: none set).
  std::optional<liquid::error> failure;
  std::string remote;
  std::uint64_t objects{};
  std::uint64_t bytes{};
  std::vector<liquid::sync_ref_outcome> references;
};

struct AttributionSnapshot {
    std::size_t line{};
    std::string actor;
    std::string change;
    bool agent{};
};

struct PresenceSnapshot {
    std::string actor;
    std::string path;
    std::optional<SourceRange> range;
    liquid::presence_activity activity{liquid::presence_activity::idle};
};

struct EditorSearchSnapshot {
    std::string query;
    std::vector<liquid::search_hit> hits;
    std::uint64_t replacements{};
    std::string error;
};

struct EditorGraphSnapshot {
  bool ready{};
  bool daemon_running{};
  bool daemon_hosted{};
  std::string error;
  std::string query_error;
  std::optional<liquid::error> query_failure;
  std::optional<liquid::daemon_status> daemon;
  liquid::graph_statistics graph;
  std::uint64_t repository_pending_drips{};
  std::vector<Diagnostic> diagnostics;
  std::optional<HoverInfo> hover;
  std::vector<CompletionCandidate> completions;
  std::optional<SymbolLocation> definition;
  std::optional<ContextBlock> ai_context;
  std::optional<SourceRange> structural_selection;
  std::vector<SourceLocation> structural_references;
  std::optional<RenamePreview> rename_preview;
  std::optional<ExtractFunctionPreview> extract_preview;
  std::optional<StructuralEditResult> structural_result;
  SourceRange ai_context_focus;
  std::uint64_t diagnostics_generation{};
  std::uint64_t hover_generation{};
  std::uint64_t completion_generation{};
  std::uint64_t definition_generation{};
  std::uint64_t ai_context_generation{};
  std::uint64_t structural_selection_generation{};
  std::uint64_t structural_references_generation{};
  std::uint64_t structural_preview_generation{};
  std::uint64_t structural_result_generation{};
  double edit_to_diagnostic_ms{};
  bool build_running{};
  // The last build or test run's failure; shown in the Build panel only.
  std::optional<liquid::error> build_error;
  std::string build_result;
  std::chrono::milliseconds build_first_action{};
  std::chrono::milliseconds build_finished{};
  std::vector<BuildActionSnapshot> build_actions;
  std::vector<liquid::critical_action> critical_path;
  std::vector<liquid::test_target> tests;
  std::vector<TestSnapshot> test_results;
  std::vector<Diagnostic> build_diagnostics;
  DeploySnapshot deploy;
  std::vector<SyntaxSpan> syntax;
  std::vector<FoldRange> folds;
  std::vector<AttributionSnapshot> attribution;
  std::vector<PresenceSnapshot> presence;
  std::optional<liquid::drive_path_status> drive;
  EditorSearchSnapshot search;
  std::string syntax_error;
  std::string history_error;
  std::uint64_t syntax_generation{};
  std::uint64_t collaboration_generation{};
  std::uint64_t search_generation{};
  std::uint64_t history_generation{};
};

class EditorGraph {
public:
    explicit EditorGraph(std::filesystem::path repository_path,
                         std::shared_ptr<PluginHost> plugins = {},
                         std::filesystem::path syntax_frontend = {});
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
    void request_editor_view(std::filesystem::path path, StubLanguage language, std::string source);
    void request_search(std::string query);
    void request_replace(std::string query, std::string replacement, bool regular_expression);
    void request_undo();
    void request_redo();
    void request_structural_selection(std::filesystem::path path,
                                      std::uint32_t byte,
                                      std::optional<SourceRange> current,
                                      bool enclosing_definition);
    void request_reference_selection(std::filesystem::path path, std::uint32_t byte);
    void request_rename(std::filesystem::path path, std::uint32_t byte, std::string new_name);
    void
    request_extract_function(std::filesystem::path path, SourceRange selection, std::string name);
    void apply_structural_edits(std::vector<StructuralEdit> edits);
    void request_ai_context(std::filesystem::path path,
                            std::uint32_t byte,
                            std::optional<SourceRange> selection,
                            std::string selected_source,
                            std::size_t whole_file_bytes,
                            std::size_t byte_budget);
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

[[nodiscard]] auto editor_graph_status(const EditorGraphSnapshot &snapshot)
    -> std::string;

} // namespace liquify
