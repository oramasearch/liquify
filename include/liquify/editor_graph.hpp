#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "liquid/daemon.hpp"
#include "liquid/graph.hpp"
#include "liquify/context_builder.hpp"
#include "liquify/interactive_analysis.hpp"

namespace liquify {

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
    std::optional<ContextBlock> ai_context;
    SourceRange ai_context_focus;
    std::uint64_t diagnostics_generation{};
    std::uint64_t hover_generation{};
    std::uint64_t completion_generation{};
    std::uint64_t definition_generation{};
    std::uint64_t ai_context_generation{};
    double edit_to_diagnostic_ms{};
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
    void request_ai_context(std::filesystem::path path,
                            std::uint32_t byte,
                            std::optional<SourceRange> selection,
                            std::string selected_source,
                            std::size_t whole_file_bytes,
                            std::size_t byte_budget);
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
