// SPDX-License-Identifier: MIT

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "liquify/interactive_analysis.hpp"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("liquify-interactive-" + std::to_string(suffix));
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

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(LIQUIFY_SOURCE_DIR) / "tests/fixtures/analysis" / name;
}

auto read_fixture(std::string_view name) -> std::string {
    auto input = std::ifstream(fixture_path(name), std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

auto byte_of(std::string_view source, std::string_view needle, std::size_t occurrence = 0)
    -> std::uint32_t {
    auto position = std::size_t{};
    for (std::size_t index = 0; index <= occurrence; ++index) {
        position = source.find(needle, position);
        EXPECT_NE(position, std::string_view::npos);
        if (position == std::string_view::npos) {
            return 0;
        }
        ++position;
    }
    return static_cast<std::uint32_t>(position - 1);
}

struct AnalysisFixture {
    TemporaryDirectory state;
    std::string source{read_fixture("interactive.cpp")};
    liquid::result<liquify::InteractiveAnalysis> analysis{
        liquify::InteractiveAnalysis::open(state.path())};

    void open() {
        ASSERT_TRUE(analysis) << analysis.error().message;
        auto opened = analysis->open_buffer("interactive.cpp", liquify::StubLanguage::cpp, source);
        ASSERT_TRUE(opened) << opened.error().message;

        TemporaryDirectory stub_state;
        auto trees = liquify::StubTrees::open(stub_state.path());
        ASSERT_TRUE(trees) << trees.error().message;
        auto provider = trees->for_file(fixture_path("provider.hpp"));
        ASSERT_TRUE(provider) << provider.error().message;
        auto added = analysis->add_stub("provider.hpp", std::move(*provider));
        ASSERT_TRUE(added) << added.error().message;
    }
};

TEST(InteractiveAnalysis, AnswersEveryCursorQueryOnInvalidInput) {
    AnalysisFixture fixture;
    fixture.open();

    const auto provide = byte_of(fixture.source, "provide", 1);
    auto node = fixture.analysis->node_at_position("interactive.cpp", provide);
    ASSERT_TRUE(node) << node.error().message;
    ASSERT_TRUE(*node);
    EXPECT_EQ((*node)->kind, "identifier");
    EXPECT_EQ((*node)->spelling, "provide");

    auto enclosing = fixture.analysis->enclosing_definition("interactive.cpp", provide);
    ASSERT_TRUE(enclosing) << enclosing.error().message;
    ASSERT_TRUE(*enclosing);
    EXPECT_EQ((*enclosing)->kind, "function_definition");
    EXPECT_NE((*enclosing)->spelling.find("calculate"), std::string::npos);

    auto definition = fixture.analysis->go_to_definition("interactive.cpp", provide);
    ASSERT_TRUE(definition) << definition.error().message;
    ASSERT_TRUE(*definition);
    EXPECT_EQ((*definition)->path, "provider.hpp");
    EXPECT_EQ((*definition)->declaration.name, "provide");
    EXPECT_EQ((*definition)->declaration.kind, liquify::DeclarationKind::function);

    const auto nearby_use = byte_of(fixture.source, "nearby", 1);
    auto references = fixture.analysis->find_references("interactive.cpp", nearby_use);
    ASSERT_TRUE(references) << references.error().message;
    EXPECT_GE(references->size(), 2U);
    EXPECT_TRUE(std::ranges::any_of(*references,
                                    [](const auto& reference) { return reference.is_definition; }));

    auto hover = fixture.analysis->hover("interactive.cpp", provide);
    ASSERT_TRUE(hover) << hover.error().message;
    ASSERT_TRUE(*hover);
    EXPECT_NE((*hover)->signature.find("provide"), std::string::npos);
    EXPECT_EQ((*hover)->documentation, "Returns a value from the closed-file provider.");

    auto completions = fixture.analysis->completion("interactive.cpp", nearby_use);
    ASSERT_TRUE(completions) << completions.error().message;
    const auto nearby =
        std::ranges::find(*completions, "nearby", &liquify::CompletionCandidate::name);
    const auto global =
        std::ranges::find(*completions, "global_value", &liquify::CompletionCandidate::name);
    const auto closed =
        std::ranges::find(*completions, "provide", &liquify::CompletionCandidate::name);
    ASSERT_NE(nearby, completions->end());
    ASSERT_NE(global, completions->end());
    ASSERT_NE(closed, completions->end());
    EXPECT_LT(nearby->rank, global->rank);
    EXPECT_LT(global->rank, closed->rank);
}

TEST(InteractiveAnalysis, PrefersCompilerSymbolRecordWhenPresent) {
    AnalysisFixture fixture;
    fixture.open();

    auto compiler_definition = liquify::StubDeclaration{liquify::DeclarationKind::variable,
                                                        "global_value",
                                                        {},
                                                        "extern int global_value;",
                                                        "Compiler-derived definition.",
                                                        {8, 20, 0, 8, 0, 20}};
    auto set =
        fixture.analysis->set_symbol_records({{"global_value",
                                               {"generated/symbols.hpp", compiler_definition},
                                               {{{"interactive.cpp"}, {128, 140, 6, 28, 6, 40}}}}});
    ASSERT_TRUE(set) << set.error().message;

    const auto use = byte_of(fixture.source, "global_value", 1);
    auto definition = fixture.analysis->go_to_definition("interactive.cpp", use);
    ASSERT_TRUE(definition) << definition.error().message;
    ASSERT_TRUE(*definition);
    EXPECT_EQ((*definition)->path, "generated/symbols.hpp");

    auto references = fixture.analysis->find_references("interactive.cpp", use);
    ASSERT_TRUE(references) << references.error().message;
    ASSERT_EQ(references->size(), 2U);
    EXPECT_TRUE(references->front().is_definition);
}

TEST(InteractiveAnalysis, ReadsTranslationUnitDiagnosticsFromTheGraph) {
    AnalysisFixture fixture;
    fixture.open();

    auto set =
        fixture.analysis->set_build_diagnostics("interactive.cpp",
                                                "build/interactive.cpp",
                                                {{{"interactive.cpp", {173, 177, 10, 9, 10, 13}},
                                                  liquify::DiagnosticSeverity::error,
                                                  "expected ';' after return expression"}});
    ASSERT_TRUE(set) << set.error().message;

    auto diagnostics = fixture.analysis->diagnostics("interactive.cpp");
    ASSERT_TRUE(diagnostics) << diagnostics.error().message;
    ASSERT_EQ(diagnostics->size(), 1U);
    EXPECT_EQ(diagnostics->front().severity, liquify::DiagnosticSeverity::error);
    EXPECT_EQ(diagnostics->front().location.path, "interactive.cpp");
}

TEST(InteractiveAnalysis, IncrementalEditReusesLeavesOutsideTheEditedRange) {
    TemporaryDirectory state;
    auto analysis = liquify::InteractiveAnalysis::open(state.path());
    ASSERT_TRUE(analysis) << analysis.error().message;
    const auto source = std::string{"int first = 1;\nint second = 2;\nint third = 3;\n"};
    auto opened = analysis->open_buffer("valid.cpp", liquify::StubLanguage::cpp, source);
    ASSERT_TRUE(opened) << opened.error().message;

    const auto literal = byte_of(source, "2");
    auto report = analysis->apply_edit("valid.cpp", {literal, literal + 1, "4"});
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_GT(report->reused_leaf_nodes, 0U);
    EXPECT_GT(report->total_leaf_nodes, report->reparsed_leaf_ranges.size());
    auto affected_ranges = report->changed_ranges;
    if (affected_ranges.empty()) {
        affected_ranges.push_back(report->edited_range);
    }
    for (const auto& range : report->reparsed_leaf_ranges) {
        EXPECT_TRUE(std::ranges::any_of(affected_ranges, [&range](const auto& affected) {
            return range.start_byte < affected.end_byte && affected.start_byte < range.end_byte;
        }));
    }
}

TEST(InteractiveAnalysis, CancelsTypingQueryAndAnswersTheEditedBuffer) {
    TemporaryDirectory state;
    auto analysis = liquify::InteractiveAnalysis::open(state.path());
    ASSERT_TRUE(analysis) << analysis.error().message;

    auto source = std::string{};
    for (std::uint32_t index = 0; index < 40'000; ++index) {
        source += "int candidate_" + std::to_string(index) + " = " + std::to_string(index) + ";\n";
    }
    source += "int use() { return candidate_39999; }\n";
    auto opened = analysis->open_buffer("typing.cpp", liquify::StubLanguage::cpp, source);
    ASSERT_TRUE(opened) << opened.error().message;

    const auto position = byte_of(source, "candidate_39999", 1);
    const auto executions = analysis->statistics().executions;
    auto pending = std::async(std::launch::async,
                              [&] { return analysis->completion("typing.cpp", position); });
    while (analysis->statistics().executions == executions) {
        std::this_thread::yield();
    }
    auto edited = analysis->apply_edit("typing.cpp", {0, 0, "int nearest = 1;\n"});
    ASSERT_TRUE(edited) << edited.error().message;

    auto cancelled = pending.get();
    EXPECT_FALSE(cancelled);
    EXPECT_GT(analysis->statistics().discarded_computations, 0U);

    auto answered = analysis->completion("typing.cpp", position + 17);
    ASSERT_TRUE(answered) << answered.error().message;
    EXPECT_NE(std::ranges::find(*answered, "nearest", &liquify::CompletionCandidate::name),
              answered->end());
}

TEST(InteractiveAnalysis, MeasuresGraphQueryLatency) {
    AnalysisFixture fixture;
    fixture.open();
    const auto provide = byte_of(fixture.source, "provide", 1);
    const auto nearby = byte_of(fixture.source, "nearby", 1);
    const auto positions = std::array<std::uint32_t, 4>{provide, nearby, provide, nearby};
    auto latency = fixture.analysis->measure_latency("interactive.cpp", positions);
    ASSERT_TRUE(latency) << latency.error().message;
    EXPECT_EQ(latency->samples, positions.size());
    EXPECT_GE(latency->go_to_definition_p95_ms, 0.0);
    EXPECT_GE(latency->completion_p95_ms, 0.0);
}

} // namespace
