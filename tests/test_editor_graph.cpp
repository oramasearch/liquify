#include <chrono>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "liquid/daemon.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/editor_graph.hpp"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("liquify-editor-graph-" + std::to_string(suffix));
        std::filesystem::remove_all(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;

    [[nodiscard]] auto path() const -> const std::filesystem::path& {
        return path_;
    }

private:
    std::filesystem::path path_;
};

auto bytes(std::string_view value) -> std::vector<std::byte> {
    const auto characters = std::span(value.data(), value.size());
    const auto source = std::as_bytes(characters);
    return {source.begin(), source.end()};
}

auto create_repository(const std::filesystem::path& path, std::string_view source)
    -> liquid::repository {
    auto created = liquid::runtime::block_on(liquid::repository::create(path));
    EXPECT_TRUE(created) << (created ? "" : created.error().message);
    auto repository = std::move(*created);
    auto written = liquid::runtime::block_on(repository.write("main.cpp", bytes(source)));
    EXPECT_TRUE(written) << (written ? "" : written.error().message);
    return repository;
}

// liquify#79: the analysis cache is per user and per repository, never inside the repository, so a
// repository cannot ship a crafted cache that the editor reads on open.
TEST(EditorGraph, AnalysisCacheLivesOutsideTheRepository) {
  const auto root = std::filesystem::temp_directory_path() / "liquify-cache-root";
  const auto repository = std::filesystem::temp_directory_path() / "liquify-cache-repo";
  const auto other = std::filesystem::temp_directory_path() / "liquify-cache-other";
  const auto cache = liquify::analysis_cache_directory(repository, root);
  EXPECT_TRUE(cache.generic_string().starts_with((root / "analysis").generic_string()));
  const auto inside = std::ranges::mismatch(repository, cache).in1 == repository.end();
  EXPECT_FALSE(inside) << cache;
  EXPECT_EQ(cache, liquify::analysis_cache_directory(repository, root)) << "stable";
  EXPECT_NE(cache, liquify::analysis_cache_directory(other, root)) << "one per repository";
}

TEST(EditorGraph, PublishesOverlayQueriesAndRepositoryStatus) {
    TemporaryDirectory location;
    const auto source = std::string{"int answer = 42;\nint use() { return answer; }\n"};
    auto repository = create_repository(location.path(), source);
    auto repository_status = repository.status();
    ASSERT_TRUE(repository_status) << repository_status.error().message;
    ASSERT_GE(repository_status->pending_drips, 1U);
    liquify::EditorGraph graph(location.path());

    graph.open_buffer("main.cpp", liquify::StubLanguage::cpp, source);
    const auto literal = static_cast<std::uint32_t>(source.find("42"));
    graph.apply_edit("main.cpp", {literal, literal + 2, "43"});
    graph.publish_build_diagnostics("main.cpp",
                                    "main.cpp",
                                    {{{"main.cpp", {15, 17, 0, 13, 0, 15}},
                                      liquify::DiagnosticSeverity::warning,
                                      "answer changed"}});
    graph.request_diagnostics("main.cpp");
    graph.request_hover("main.cpp", 40);
    graph.request_completion("main.cpp", 40);
    graph.request_definition("main.cpp", 40);
    graph.note_saved_drip(repository_status->pending_drips);
    graph.refresh_status();
    graph.wait_until_idle();

    const auto snapshot = graph.snapshot();
    ASSERT_TRUE(snapshot.ready) << snapshot.error;
#ifndef _WIN32
    EXPECT_TRUE(snapshot.daemon_running);
    EXPECT_TRUE(snapshot.daemon_hosted);
#endif
    ASSERT_EQ(snapshot.diagnostics.size(), 1U);
    EXPECT_EQ(snapshot.diagnostics.front().message, "answer changed");
    EXPECT_GT(snapshot.edit_to_diagnostic_ms, 0.0);
    ASSERT_TRUE(snapshot.hover);
    EXPECT_NE(snapshot.hover->signature.find("answer"), std::string::npos);
    EXPECT_NE(
        std::ranges::find(snapshot.completions, "answer", &liquify::CompletionCandidate::name),
        snapshot.completions.end());
    ASSERT_TRUE(snapshot.definition);
    EXPECT_EQ(snapshot.definition->path, "main.cpp");
    EXPECT_GE(snapshot.repository_pending_drips, 1U);
    EXPECT_NE(liquify::editor_graph_status(snapshot).find("validation backlog="),
              std::string::npos);
}

TEST(EditorGraph, PresentsBuildEventsTestsAndRefusedReferences) {
    TemporaryDirectory location;
    auto repository = create_repository(location.path(), "int answer = 42;\n");
    static_cast<void>(repository);
    liquify::EditorGraph graph(location.path());
    graph.wait_until_idle();

    graph.open_buffer("main.cpp", liquify::StubLanguage::cpp, "int answer = 42;\n");
    graph.publish_build_event({.kind = liquid::build_event_kind::action_queued, .sequence = 1});
    graph.publish_build_event(
        {.kind = liquid::build_event_kind::action_started,
         .sequence = 2,
         .execution = liquid::action_execution{liquid::action_execution_kind::remote, "worker-a"}});
    graph.publish_build_event(
        {.kind = liquid::build_event_kind::action_failed,
         .sequence = 3,
         .diagnostics = {{.file = "main.cpp",
                          .line = 1,
                          .column = 5,
                          .severity = liquid::frontend_diagnostic_severity::error,
                          .message = "expected expression"}}});
    graph.publish_build_event(
        {.kind = liquid::build_event_kind::test_result,
         .sequence = 4,
         .test = liquid::test_result{"alpha", liquid::test_status::passed, "", {}, false}});
    graph.publish_build_event(
        {.kind = liquid::build_event_kind::test_result,
         .sequence = 5,
         .test = liquid::test_result{"alpha", liquid::test_status::passed, "", {}, true}});
    auto snapshot = graph.snapshot();
    ASSERT_EQ(snapshot.test_results.size(), 1U);
    EXPECT_TRUE(snapshot.test_results.front().hit);
    graph.publish_build_event(
        {.kind = liquid::build_event_kind::test_result,
         .sequence = 6,
         .test = liquid::test_result{"alpha", liquid::test_status::passed, "", {}, false}});
    graph.publish_deploy_report(
        {.sent = {.objects = 3, .bytes = 96},
         .references = {{"main", liquid::sync_ref_outcome_kind::refused, "protected reference"},
                        {"release", liquid::sync_ref_outcome_kind::applied, ""}}});

    snapshot = graph.snapshot();
    ASSERT_EQ(snapshot.build_actions.size(), 1U);
    EXPECT_EQ(snapshot.build_actions.front().state, liquify::BuildActionState::failed);
    EXPECT_EQ(snapshot.build_actions.front().worker, "worker-a");
    ASSERT_EQ(snapshot.build_diagnostics.size(), 1U);
    EXPECT_EQ(snapshot.build_diagnostics.front().location.path, "main.cpp");
    EXPECT_EQ(snapshot.build_diagnostics.front().location.range.start_line, 0U);
    EXPECT_FALSE(snapshot.test_results.front().hit);
    ASSERT_EQ(snapshot.deploy.references.size(), 2U);
    EXPECT_EQ(snapshot.deploy.references.front().outcome, liquid::sync_ref_outcome_kind::refused);
    EXPECT_EQ(snapshot.deploy.references.front().reason, "protected reference");
    EXPECT_EQ(snapshot.deploy.objects, 3U);
    EXPECT_EQ(snapshot.deploy.bytes, 96U);
}

TEST(EditorGraph, PublishesSyntaxGuttersSearchAndRepositoryHistory) {
    TemporaryDirectory location;
    constexpr auto source = "int answer() {\n  return 42;\n}\n";
    auto repository = create_repository(location.path(), source);
    auto initial = liquid::runtime::block_on(repository.cut("initial editor source"));
    ASSERT_TRUE(initial) << initial.error().message;
    liquify::EditorGraph graph(location.path(), {}, LIQUIFY_TREE_SITTER_FRONTEND);
    graph.wait_until_idle();

    graph.request_editor_view("main.cpp", liquify::StubLanguage::cpp, source);
    graph.request_search("answer");
    graph.wait_until_idle();
    auto snapshot = graph.snapshot();
    EXPECT_TRUE(snapshot.syntax_error.empty()) << snapshot.syntax_error;
    EXPECT_FALSE(snapshot.syntax.empty());
    EXPECT_FALSE(snapshot.folds.empty());
    EXPECT_FALSE(snapshot.attribution.empty());
    ASSERT_EQ(snapshot.search.hits.size(), 1U) << snapshot.search.error;
    EXPECT_EQ(snapshot.search.hits.front().path, "main.cpp");

    graph.request_replace("answer", "result", false);
    graph.wait_until_idle();
    snapshot = graph.snapshot();
    EXPECT_EQ(snapshot.search.replacements, 1U) << snapshot.search.error;
    auto reopened = liquid::runtime::block_on(liquid::repository::open(location.path()));
    ASSERT_TRUE(reopened) << reopened.error().message;
    auto changed = liquid::runtime::block_on(reopened->read("main.cpp"));
    ASSERT_TRUE(changed) << changed.error().message;
    EXPECT_NE(
        std::string(reinterpret_cast<const char*>(changed->data()), changed->size()).find("result"),
        std::string::npos);

    graph.request_undo();
    graph.wait_until_idle();
    snapshot = graph.snapshot();
    EXPECT_TRUE(snapshot.history_error.empty()) << snapshot.history_error;
    auto restored_repository = liquid::runtime::block_on(liquid::repository::open(location.path()));
    ASSERT_TRUE(restored_repository) << restored_repository.error().message;
    auto restored = liquid::runtime::block_on(restored_repository->read("main.cpp"));
    ASSERT_TRUE(restored) << restored.error().message;
    EXPECT_NE(std::string(reinterpret_cast<const char*>(restored->data()), restored->size())
                  .find("answer"),
              std::string::npos);

    graph.request_redo();
    graph.wait_until_idle();
    snapshot = graph.snapshot();
    EXPECT_TRUE(snapshot.history_error.empty()) << snapshot.history_error;
    auto redone_repository = liquid::runtime::block_on(liquid::repository::open(location.path()));
    ASSERT_TRUE(redone_repository) << redone_repository.error().message;
    auto redone = liquid::runtime::block_on(redone_repository->read("main.cpp"));
    ASSERT_TRUE(redone) << redone.error().message;
    EXPECT_NE(
        std::string(reinterpret_cast<const char*>(redone->data()), redone->size()).find("result"),
        std::string::npos);
}

#ifndef _WIN32
TEST(EditorGraph, AttachesWhenTheDaemonAlreadyOwnsTheCheckout) {
    TemporaryDirectory location;
    auto repository = create_repository(location.path(), "int main() { return 0; }\n");
    static_cast<void>(repository);
    auto options = liquid::daemon_options{};
    options.capture.exclude.push_back(".liquid/**");
    auto hosted = liquid::runtime::block_on(liquid::daemon::start(location.path(), options));
    ASSERT_TRUE(hosted) << (hosted ? "" : hosted.error().message);

    {
        liquify::EditorGraph graph(location.path());
        graph.wait_until_idle();
        const auto snapshot = graph.snapshot();
        ASSERT_TRUE(snapshot.ready) << snapshot.error;
        EXPECT_TRUE(snapshot.daemon_running);
        EXPECT_FALSE(snapshot.daemon_hosted);
    }

    EXPECT_TRUE(liquid::runtime::block_on(hosted->shutdown()));
    EXPECT_TRUE(liquid::runtime::block_on(hosted->wait()));
}
#endif

} // namespace
