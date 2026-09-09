#pragma once

#include <array>
#include <cstdint>
#include <map>

#include "liquify/input.hpp"
#include "liquify/panel.hpp"

namespace liquify {

using UiPixelColor = std::array<std::uint8_t, 4>;

// Ink bounds come from submitted glyph vertices, in window logical coordinates.
struct UiTextGeometry {
  InputRectangle ink;
  float line_height{};
};

struct UiPanelGeometry {
  InputRectangle content;
  InputRectangle tab;
  bool selected{};
  UiTextGeometry text;
};

// Replaced every frame; a hidden or unsubmitted surface has empty bounds.
struct UiFrameLayout {
  InputRectangle window;
  InputPoint drawable_scale;
  UiPixelColor canvas{};
  UiPixelColor surface{};
  UiPixelColor selected{};
  InputRectangle toolbar;
  UiTextGeometry toolbar_text;
  InputRectangle editor;
  UiTextGeometry editor_text;
  std::map<PanelId, UiPanelGeometry> panels;
};

} // namespace liquify
