// SPDX-License-Identifier: MIT

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "liquify/editor_model.hpp"

namespace {

TEST(EditorModel, ReadsRepositoryIndentAndWrapConfiguration) {
    constexpr auto config = R"config(
root = true

[*]
indent_style = tab
tab_width = 8

[*.cpp]
indent_style = space
indent_size = 2
max_line_length = 100
)config";

    const auto cpp = liquify::parse_editor_preferences(config, "src/app.cpp");
    EXPECT_EQ(cpp.indent_style, liquify::IndentStyle::spaces);
    EXPECT_EQ(cpp.indent_size, 2U);
    EXPECT_TRUE(cpp.soft_wrap);
    EXPECT_EQ(liquify::indentation_text(cpp), "  ");

    const auto text = liquify::parse_editor_preferences(config, "README.md");
    EXPECT_EQ(text.indent_style, liquify::IndentStyle::tabs);
    EXPECT_EQ(text.indent_size, 8U);
    EXPECT_EQ(liquify::indentation_text(text), "\t");
}

TEST(EditorModel, SelectsCharactersWordsLinesAndColumns) {
    const auto lines = std::array<std::string, 3>{"alpha beta", "short", "third line"};

    EXPECT_EQ(liquify::selection_for(lines, {0, 0}, liquify::SelectionKind::character),
              (liquify::TextSelection{{0, 0}, {0, 1}}));
    EXPECT_EQ(liquify::selection_for(lines, {0, 7}, liquify::SelectionKind::word),
              (liquify::TextSelection{{0, 6}, {0, 10}}));
    EXPECT_EQ(liquify::selection_for(lines, {1, 2}, liquify::SelectionKind::line),
              (liquify::TextSelection{{1, 0}, {1, 5}}));

    const auto columns = liquify::column_selections(lines, {0, 2}, {2, 7});
    ASSERT_EQ(columns.size(), 3U);
    EXPECT_EQ(columns[0], (liquify::TextSelection{{0, 2}, {0, 7}}));
    EXPECT_EQ(columns[1], (liquify::TextSelection{{1, 2}, {1, 5}}));
    EXPECT_EQ(columns[2], (liquify::TextSelection{{2, 2}, {2, 7}}));
}

TEST(EditorModel, FindsAndReplacesLiteralsAndRegularExpressions) {
    constexpr auto source = "Alpha 12\nalpha 34\n";
    const auto insensitive = liquify::find_text(source, "alpha");
    ASSERT_EQ(insensitive.size(), 2U);
    EXPECT_EQ(insensitive[1].range, (liquify::TextSelection{{1, 0}, {1, 5}}));

    const auto numbers =
        liquify::find_text(source, "[0-9]+", {.regular_expression = true, .case_sensitive = true});
    ASSERT_EQ(numbers.size(), 2U);
    EXPECT_EQ(numbers[0].start_byte, 6U);
    EXPECT_EQ(liquify::replace_text(
                  source, "([0-9]+)", "<$1>", {.regular_expression = true, .case_sensitive = true}),
              "Alpha <12>\nalpha <34>\n");
    EXPECT_EQ(liquify::replace_text("cost $5", "$5", "$6"), "cost $6");
}

TEST(EditorModel, MatchesNestedBracketsAndClassifiesSyntax) {
    constexpr auto source = "call(values[2])";
    EXPECT_EQ(liquify::matching_bracket(source, 4), 14U);
    EXPECT_EQ(liquify::matching_bracket(source, 12), 13U);
    EXPECT_FALSE(liquify::matching_bracket(source, 0));

    EXPECT_EQ(liquify::syntax_token_kind("return", "return"), liquify::SyntaxTokenKind::keyword);
    EXPECT_EQ(liquify::syntax_token_kind("string_literal", "\"value\""),
              liquify::SyntaxTokenKind::string);
    EXPECT_EQ(liquify::syntax_token_kind("comment", "// value"), liquify::SyntaxTokenKind::comment);
}

TEST(EditorModel, WrapsLinesAndCollapsesFoldedRanges) {
    const auto lines =
        std::array<std::string, 4>{"one two three four", "inside", "inside again", "tail"};
    const auto wrapped = liquify::visual_rows(lines, 8, true);
    ASSERT_GT(wrapped.size(), lines.size());
    EXPECT_TRUE(wrapped[1].continuation);

    const auto folds = std::array<liquify::FoldRange, 1>{{{0, 2, "function"}}};
    const auto folded = liquify::visual_rows(lines, 80, false, folds);
    ASSERT_EQ(folded.size(), 2U);
    EXPECT_TRUE(folded.front().folded);
    EXPECT_EQ(folded.back().source_line, 3U);
}

} // namespace
