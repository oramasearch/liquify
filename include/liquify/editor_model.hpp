#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/frontend.hpp"

namespace liquify {

struct TextPosition {
    std::size_t line{};
    std::size_t byte{};

    auto operator<=>(const TextPosition&) const = default;
};

struct TextSelection {
    TextPosition begin;
    TextPosition end;

    auto operator==(const TextSelection&) const -> bool = default;
};

enum class SelectionKind : std::uint8_t {
    character,
    word,
    line,
    column,
};

enum class IndentStyle : std::uint8_t {
    tabs,
    spaces,
};

struct EditorPreferences {
    IndentStyle indent_style{IndentStyle::spaces};
    std::size_t indent_size{4};
    bool soft_wrap{};

    auto operator==(const EditorPreferences&) const -> bool = default;
};

struct FindOptions {
    bool regular_expression{};
    bool case_sensitive{};

    auto operator==(const FindOptions&) const -> bool = default;
};

struct TextMatch {
    std::size_t start_byte{};
    std::size_t end_byte{};
    TextSelection range;

    auto operator==(const TextMatch&) const -> bool = default;
};

enum class SyntaxTokenKind : std::uint8_t {
    plain,
    keyword,
    type,
    function,
    variable,
    string,
    number,
    comment,
    preprocessor,
    punctuation,
};

struct SyntaxSpan {
    std::size_t start_byte{};
    std::size_t end_byte{};
    SyntaxTokenKind kind{SyntaxTokenKind::plain};

    auto operator==(const SyntaxSpan&) const -> bool = default;
};

struct FoldRange {
    std::size_t first_line{};
    std::size_t last_line{};
    std::string label;

    auto operator==(const FoldRange&) const -> bool = default;
};

struct VisualRow {
    std::size_t source_line{};
    std::size_t start_byte{};
    std::size_t end_byte{};
    bool continuation{};
    bool folded{};

    auto operator==(const VisualRow&) const -> bool = default;
};

[[nodiscard]] auto parse_editor_preferences(std::string_view contents, std::string_view path)
    -> EditorPreferences;
[[nodiscard]] auto indentation_text(const EditorPreferences& preferences) -> std::string;
[[nodiscard]] auto selection_for(std::span<const std::string> lines,
                                 TextPosition position,
                                 SelectionKind kind) -> TextSelection;
[[nodiscard]] auto column_selections(std::span<const std::string> lines,
                                     TextPosition anchor,
                                     TextPosition cursor) -> std::vector<TextSelection>;
[[nodiscard]] auto find_text(std::string_view text,
                             std::string_view query,
                             FindOptions options = {}) -> std::vector<TextMatch>;
[[nodiscard]] auto replace_text(std::string_view text,
                                std::string_view query,
                                std::string_view replacement,
                                FindOptions options = {}) -> std::string;
[[nodiscard]] auto syntax_token_kind(std::string_view node_kind, std::string_view spelling)
    -> SyntaxTokenKind;
[[nodiscard]] auto syntax_spans(const liquid::syntax_tree& tree, std::string_view source)
    -> std::vector<SyntaxSpan>;
[[nodiscard]] auto fold_ranges(const liquid::syntax_tree& tree) -> std::vector<FoldRange>;
[[nodiscard]] auto matching_bracket(std::string_view text, std::size_t byte)
    -> std::optional<std::size_t>;
[[nodiscard]] auto visual_rows(std::span<const std::string> lines,
                               std::size_t wrap_columns,
                               bool soft_wrap,
                               std::span<const FoldRange> folded = {}) -> std::vector<VisualRow>;

} // namespace liquify
