#include "text_layout.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace liquify {
namespace {

struct DecodedCodepoint {
    std::uint32_t value{};
    std::size_t length{1};
};

auto decode(std::string_view text, std::size_t byte) -> DecodedCodepoint {
    const auto lead = static_cast<unsigned char>(text[byte]);
    if (lead < 0x80U) {
        return {lead, 1};
    }

    std::size_t length = 0;
    std::uint32_t value = 0;
    if ((lead & 0xe0U) == 0xc0U) {
        length = 2;
        value = lead & 0x1fU;
    } else if ((lead & 0xf0U) == 0xe0U) {
        length = 3;
        value = lead & 0x0fU;
    } else if ((lead & 0xf8U) == 0xf0U) {
        length = 4;
        value = lead & 0x07U;
    } else {
        return {0xfffdU, 1};
    }

    if (byte + length > text.size()) {
        return {0xfffdU, 1};
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
        const auto continuation = static_cast<unsigned char>(text[byte + offset]);
        if ((continuation & 0xc0U) != 0x80U) {
            return {0xfffdU, 1};
        }
        value = (value << 6U) | (continuation & 0x3fU);
    }
    return {value, length};
}

} // namespace

auto LineLayout::column_for_byte(std::size_t byte) const -> float {
    const auto found = std::lower_bound(
        glyphs.begin(), glyphs.end(), byte, [](const LayoutGlyph& glyph, std::size_t value) {
            return glyph.byte < value;
        });
    if (found == glyphs.end()) {
        return columns;
    }
    return found->column;
}

auto LineLayout::byte_for_column(float column, std::size_t line_bytes) const -> std::size_t {
    if (glyphs.empty() || column >= columns) {
        return line_bytes;
    }
    const auto found = std::lower_bound(
        glyphs.begin(), glyphs.end(), column, [](const LayoutGlyph& glyph, float value) {
            return glyph.column + 0.5F < value;
        });
    if (found == glyphs.end()) {
        return line_bytes;
    }
    return found->byte;
}

void TextLayoutCache::reset(std::string blob_id, std::size_t line_count) {
    blob_id_ = std::move(blob_id);
    lines_.clear();
    lines_.resize(line_count);
}

void TextLayoutCache::apply_change(BufferChange change) {
    const auto first = lines_.begin() + static_cast<std::ptrdiff_t>(change.first_line);
    const auto last = first + static_cast<std::ptrdiff_t>(change.erased_lines);
    const auto insertion = lines_.erase(first, last);
    lines_.insert(insertion, change.inserted_lines, std::nullopt);
}

auto TextLayoutCache::line(std::size_t index, std::string_view text) -> const LineLayout& {
    auto& cached = lines_.at(index);
    if (!cached) {
        cached = build(text);
    }
    return *cached;
}

auto TextLayoutCache::blob_id() const -> std::string_view {
    return blob_id_;
}

auto TextLayoutCache::line_count() const -> std::size_t {
    return lines_.size();
}

auto TextLayoutCache::build_count() const -> std::uint64_t {
    return build_count_;
}

auto TextLayoutCache::build(std::string_view text) -> LineLayout {
    LineLayout result;
    result.generation = next_generation_++;
    float column = 0.0F;
    for (std::size_t byte = 0; byte < text.size();) {
        const auto codepoint = decode(text, byte);
        result.glyphs.push_back(LayoutGlyph{codepoint.value, byte, column});
        if (codepoint.value == '\t') {
            constexpr float tab_width = 4.0F;
            column += tab_width - static_cast<float>(static_cast<int>(column) % 4);
        } else {
            column += 1.0F;
        }
        byte += codepoint.length;
    }
    result.columns = column;
    ++build_count_;
    return result;
}

} // namespace liquify
