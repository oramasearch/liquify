#include "liquify/editor_model.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <set>
#include <tuple>

namespace liquify {
namespace {

auto trim(std::string_view value) -> std::string_view {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

auto lowercase(std::string_view value) -> std::string {
    auto output = std::string(value);
    std::ranges::transform(output, output.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return output;
}

auto matches_section(std::string_view section, std::string_view path) -> bool {
    section = trim(section);
    if (section.empty() || section == "*") {
        return true;
    }
    if (section.starts_with("*.")) {
        return path.ends_with(section.substr(1));
    }
    if (section.front() == '{' && section.back() == '}') {
        section.remove_prefix(1);
        section.remove_suffix(1);
        while (!section.empty()) {
            const auto comma = section.find(',');
            const auto candidate = trim(section.substr(0, comma));
            if (path.ends_with(candidate)) {
                return true;
            }
            if (comma == std::string_view::npos) {
                break;
            }
            section.remove_prefix(comma + 1);
        }
    }
    return path.ends_with(section);
}

auto parse_size(std::string_view value) -> std::optional<std::size_t> {
    value = trim(value);
    auto result = std::size_t{};
    if (value.empty()) {
        return std::nullopt;
    }
    for (const auto character : value) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            return std::nullopt;
        }
        result = result * 10 + static_cast<std::size_t>(character - '0');
    }
    if (result == 0 || result > 16) {
        return std::nullopt;
    }
    return result;
}

auto is_word_byte(unsigned char value) -> bool {
    return std::isalnum(value) || value == '_' || (value & 0x80U) != 0;
}

auto line_offsets(std::string_view text) -> std::vector<std::size_t> {
    auto output = std::vector<std::size_t>{0};
    for (std::size_t byte = 0; byte < text.size(); ++byte) {
        if (text[byte] == '\n') {
            output.push_back(byte + 1);
        }
    }
    return output;
}

auto position_for(std::span<const std::size_t> offsets, std::string_view text, std::size_t byte)
    -> TextPosition {
    const auto bounded = std::min(byte, text.size());
    const auto upper = std::ranges::upper_bound(offsets, bounded);
    const auto line = upper == offsets.begin()
                          ? std::size_t{}
                          : static_cast<std::size_t>(upper - offsets.begin() - 1);
    return {line, bounded - offsets[line]};
}

auto regex_for(std::string_view query, FindOptions options) -> std::regex {
    auto flags = std::regex_constants::ECMAScript;
    if (!options.case_sensitive) {
        flags |= std::regex_constants::icase;
    }
    if (options.regular_expression) {
        return std::regex(std::string(query), flags);
    }
    auto escaped = std::string{};
    escaped.reserve(query.size() * 2);
    constexpr auto special = std::string_view{"\\.^$|()[]{}*+?"};
    for (const auto character : query) {
        if (special.find(character) != std::string_view::npos) {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return std::regex(escaped, flags);
}

auto is_fold_kind(std::string_view kind) -> bool {
    constexpr auto candidates = std::to_array<std::string_view>({
        "compound_statement",
        "declaration_list",
        "enum_specifier",
        "field_declaration_list",
        "function_definition",
        "initializer_list",
        "namespace_definition",
        "preproc_if",
        "preproc_ifdef",
        "preproc_ifndef",
        "struct_specifier",
        "switch_statement",
        "union_specifier",
    });
    return std::ranges::find(candidates, kind) != candidates.end();
}

void append_syntax_spans(const liquid::syntax_node& node,
                         std::string_view source,
                         std::vector<SyntaxSpan>& output) {
    if (node.children.empty()) {
        const auto start = std::min<std::size_t>(node.range.start.byte, source.size());
        const auto end = std::min<std::size_t>(node.range.end.byte, source.size());
        if (start < end) {
            output.push_back(
                {start, end, syntax_token_kind(node.kind, source.substr(start, end - start))});
        }
        return;
    }
    for (const auto& child : node.children) {
        append_syntax_spans(child, source, output);
    }
}

void append_fold_ranges(const liquid::syntax_node& node, std::vector<FoldRange>& output) {
    if (is_fold_kind(node.kind) && node.range.end.line > node.range.start.line) {
        output.push_back({static_cast<std::size_t>(node.range.start.line - 1),
                          static_cast<std::size_t>(node.range.end.line - 1),
                          node.kind});
    }
    for (const auto& child : node.children) {
        append_fold_ranges(child, output);
    }
}

} // namespace

auto parse_editor_preferences(std::string_view contents, std::string_view path)
    -> EditorPreferences {
    auto output = EditorPreferences{};
    auto section_applies = true;
    while (!contents.empty()) {
        const auto newline = contents.find('\n');
        auto line = trim(contents.substr(0, newline));
        if (newline == std::string_view::npos) {
            contents = {};
        } else {
            contents.remove_prefix(newline + 1);
        }
        if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section_applies = matches_section(line.substr(1, line.size() - 2), path);
            continue;
        }
        if (!section_applies) {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string_view::npos) {
            continue;
        }
        const auto name = lowercase(trim(line.substr(0, separator)));
        const auto value = lowercase(trim(line.substr(separator + 1)));
        if (name == "indent_style") {
            output.indent_style = value == "tab" ? IndentStyle::tabs : IndentStyle::spaces;
        } else if (name == "indent_size" || name == "tab_width") {
            if (const auto size = parse_size(value)) {
                output.indent_size = *size;
            }
        } else if (name == "max_line_length") {
            output.soft_wrap = value != "off" && value != "unset" &&
                               std::ranges::all_of(value, [](unsigned char character) {
                                   return std::isdigit(character);
                               });
        }
    }
    return output;
}

auto indentation_text(const EditorPreferences& preferences) -> std::string {
    if (preferences.indent_style == IndentStyle::tabs) {
        return "\t";
    }
    return std::string(preferences.indent_size, ' ');
}

auto selection_for(std::span<const std::string> lines, TextPosition position, SelectionKind kind)
    -> TextSelection {
    if (lines.empty()) {
        return {};
    }
    position.line = std::min(position.line, lines.size() - 1);
    const auto& line = lines[position.line];
    position.byte = std::min(position.byte, line.size());
    if (kind == SelectionKind::character || kind == SelectionKind::column) {
        const auto end = std::min(position.byte + 1, line.size());
        return {position, {position.line, end}};
    }
    if (kind == SelectionKind::line) {
        return {{position.line, 0}, {position.line, line.size()}};
    }
    auto begin = position.byte;
    auto end = position.byte;
    if (begin == line.size() && begin > 0) {
        --begin;
        end = begin + 1;
    }
    const auto word = begin < line.size() && is_word_byte(static_cast<unsigned char>(line[begin]));
    while (begin > 0 && is_word_byte(static_cast<unsigned char>(line[begin - 1])) == word) {
        --begin;
    }
    while (end < line.size() && is_word_byte(static_cast<unsigned char>(line[end])) == word) {
        ++end;
    }
    return {{position.line, begin}, {position.line, end}};
}

auto column_selections(std::span<const std::string> lines, TextPosition anchor, TextPosition cursor)
    -> std::vector<TextSelection> {
    auto output = std::vector<TextSelection>{};
    if (lines.empty()) {
        return output;
    }
    const auto first_line = std::min({anchor.line, cursor.line, lines.size() - 1});
    const auto last_line = std::min(std::max(anchor.line, cursor.line), lines.size() - 1);
    const auto first_byte = std::min(anchor.byte, cursor.byte);
    const auto last_byte = std::max(anchor.byte, cursor.byte);
    output.reserve(last_line - first_line + 1);
    for (auto line = first_line; line <= last_line; ++line) {
        output.push_back({{line, std::min(first_byte, lines[line].size())},
                          {line, std::min(last_byte, lines[line].size())}});
    }
    return output;
}

auto find_text(std::string_view text, std::string_view query, FindOptions options)
    -> std::vector<TextMatch> {
    auto output = std::vector<TextMatch>{};
    if (query.empty()) {
        return output;
    }
    try {
        const auto expression = regex_for(query, options);
        const auto source = std::string(text);
        const auto offsets = line_offsets(source);
        for (auto match = std::sregex_iterator(source.begin(), source.end(), expression);
             match != std::sregex_iterator{};
             ++match) {
            const auto start = static_cast<std::size_t>(match->position());
            const auto end = start + static_cast<std::size_t>(match->length());
            output.push_back(
                {start,
                 end,
                 {position_for(offsets, source, start), position_for(offsets, source, end)}});
            if (match->length() == 0 && end == source.size()) {
                break;
            }
        }
    } catch (const std::regex_error&) {
        return {};
    }
    return output;
}

auto replace_text(std::string_view text,
                  std::string_view query,
                  std::string_view replacement,
                  FindOptions options) -> std::string {
    if (query.empty()) {
        return std::string(text);
    }
    try {
        const auto expression = regex_for(query, options);
        auto encoded_replacement = std::string(replacement);
        if (!options.regular_expression) {
            std::size_t offset = 0;
            while ((offset = encoded_replacement.find('$', offset)) != std::string::npos) {
                encoded_replacement.insert(offset, 1, '$');
                offset += 2;
            }
        }
        return std::regex_replace(std::string(text), expression, encoded_replacement);
    } catch (const std::regex_error&) {
        return std::string(text);
    }
}

auto syntax_token_kind(std::string_view node_kind, std::string_view spelling) -> SyntaxTokenKind {
    const auto kind = lowercase(node_kind);
    if (kind.find("comment") != std::string::npos) {
        return SyntaxTokenKind::comment;
    }
    if (kind.find("string") != std::string::npos || kind.find("char") != std::string::npos) {
        return SyntaxTokenKind::string;
    }
    if (kind.find("number") != std::string::npos || kind.find("literal") != std::string::npos) {
        return SyntaxTokenKind::number;
    }
    if (kind.find("preproc") != std::string::npos || spelling.starts_with('#')) {
        return SyntaxTokenKind::preprocessor;
    }
    if (kind.find("type") != std::string::npos || kind == "primitive_type") {
        return SyntaxTokenKind::type;
    }
    if (kind.find("function") != std::string::npos || kind == "field_identifier") {
        return SyntaxTokenKind::function;
    }
    if (kind == "identifier") {
        return SyntaxTokenKind::variable;
    }
    constexpr auto keywords = std::to_array<std::string_view>({
        "alignas",   "alignof",   "auto",    "break",  "case",    "catch",     "class",  "const",
        "constexpr", "continue",  "default", "delete", "do",      "else",      "enum",   "explicit",
        "extern",    "for",       "friend",  "if",     "inline",  "namespace", "new",    "noexcept",
        "private",   "protected", "public",  "return", "sizeof",  "static",    "struct", "switch",
        "template",  "this",      "throw",   "try",    "typedef", "typename",  "union",  "using",
        "virtual",   "volatile",  "while",
    });
    if (std::ranges::find(keywords, spelling) != keywords.end()) {
        return SyntaxTokenKind::keyword;
    }
    if (!node_kind.empty() && !std::isalnum(static_cast<unsigned char>(node_kind.front())) &&
        node_kind.front() != '_') {
        return SyntaxTokenKind::punctuation;
    }
    return SyntaxTokenKind::plain;
}

auto syntax_spans(const liquid::syntax_tree& tree, std::string_view source)
    -> std::vector<SyntaxSpan> {
    auto output = std::vector<SyntaxSpan>{};
    append_syntax_spans(tree.root, source, output);
    std::ranges::sort(output, {}, &SyntaxSpan::start_byte);
    return output;
}

auto fold_ranges(const liquid::syntax_tree& tree) -> std::vector<FoldRange> {
    auto output = std::vector<FoldRange>{};
    append_fold_ranges(tree.root, output);
    std::ranges::sort(output, [](const auto& left, const auto& right) {
        return std::tie(left.first_line, left.last_line) <
               std::tie(right.first_line, right.last_line);
    });
    output.erase(std::unique(output.begin(), output.end()), output.end());
    return output;
}

auto matching_bracket(std::string_view text, std::size_t byte) -> std::optional<std::size_t> {
    if (text.empty()) {
        return std::nullopt;
    }
    if (byte == text.size() ||
        std::string_view{"()[]{}"}.find(text[byte]) == std::string_view::npos) {
        if (byte == 0 ||
            std::string_view{"()[]{}"}.find(text[byte - 1]) == std::string_view::npos) {
            return std::nullopt;
        }
        --byte;
    }
    const auto opening = std::string_view{"([{"};
    const auto closing = std::string_view{")]}"};
    const auto opening_index = opening.find(text[byte]);
    const auto closing_index = closing.find(text[byte]);
    const auto forward = opening_index != std::string_view::npos && opening_index < 3;
    const auto pair_index = forward ? opening_index : closing_index;
    if (pair_index == std::string_view::npos || pair_index >= 3) {
        return std::nullopt;
    }
    const auto open = opening[pair_index];
    const auto close = closing[pair_index];
    auto depth = 0;
    if (forward) {
        for (auto index = byte; index < text.size(); ++index) {
            if (text[index] == open) {
                ++depth;
            } else if (text[index] == close && --depth == 0) {
                return index;
            }
        }
        return std::nullopt;
    }
    for (auto index = byte + 1; index > 0; --index) {
        const auto current = index - 1;
        if (text[current] == close) {
            ++depth;
        } else if (text[current] == open && --depth == 0) {
            return current;
        }
    }
    return std::nullopt;
}

auto visual_rows(std::span<const std::string> lines,
                 std::size_t wrap_columns,
                 bool soft_wrap,
                 std::span<const FoldRange> folded) -> std::vector<VisualRow> {
    auto output = std::vector<VisualRow>{};
    const auto width = std::max<std::size_t>(wrap_columns, 1);
    for (std::size_t line = 0; line < lines.size(); ++line) {
        const auto fold = std::ranges::find(folded, line, &FoldRange::first_line);
        if (fold != folded.end() && fold->last_line > line) {
            output.push_back({line, 0, lines[line].size(), false, true});
            line = std::min(fold->last_line, lines.size() - 1);
            continue;
        }
        if (!soft_wrap || lines[line].size() <= width) {
            output.push_back({line, 0, lines[line].size(), false, false});
            continue;
        }
        auto begin = std::size_t{};
        while (begin < lines[line].size()) {
            auto end = std::min(begin + width, lines[line].size());
            if (end < lines[line].size()) {
                const auto space = lines[line].rfind(' ', end);
                if (space != std::string::npos && space > begin) {
                    end = space + 1;
                }
            }
            output.push_back({line, begin, end, begin != 0, false});
            begin = end;
        }
    }
    if (output.empty()) {
        output.push_back({});
    }
    return output;
}

} // namespace liquify
