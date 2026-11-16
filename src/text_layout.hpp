// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "editor_buffer.hpp"

namespace liquify {

struct LayoutGlyph {
    std::uint32_t codepoint{};
    std::size_t byte{};
    float column{};
};

struct LineLayout {
    std::vector<LayoutGlyph> glyphs;
    float columns{};
    std::uint64_t generation{};

    [[nodiscard]] auto column_for_byte(std::size_t byte) const -> float;
    [[nodiscard]] auto byte_for_column(float column, std::size_t line_bytes) const -> std::size_t;
};

class TextLayoutCache {
public:
    void reset(std::string blob_id, std::size_t line_count);
    void apply_change(BufferChange change);

    [[nodiscard]] auto line(std::size_t index, std::string_view text) -> const LineLayout&;
    [[nodiscard]] auto blob_id() const -> std::string_view;
    [[nodiscard]] auto line_count() const -> std::size_t;
    [[nodiscard]] auto build_count() const -> std::uint64_t;

private:
    [[nodiscard]] auto build(std::string_view text) -> LineLayout;

    std::string blob_id_;
    std::vector<std::optional<LineLayout>> lines_;
    std::uint64_t next_generation_{1};
    std::uint64_t build_count_{};
};

} // namespace liquify
