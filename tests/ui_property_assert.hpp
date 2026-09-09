#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include "liquify/screenshot.hpp"
#include "liquify/ui_layout.hpp"

namespace liquify::test {

inline void require_property(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline void require_bounds(InputRectangle bounds, InputRectangle parent,
                           std::string_view name) {
  const auto valid = std::isfinite(bounds.x) && std::isfinite(bounds.y) &&
                     std::isfinite(bounds.width) &&
                     std::isfinite(bounds.height) && bounds.width > 0 &&
                     bounds.height > 0;
  require_property(valid,
                   std::string(name) + " did not report non-empty bounds");
  // One logical pixel permits integer clip rounding, independently of layout.
  require_property(bounds.x >= parent.x - 1 && bounds.y >= parent.y - 1 &&
                       bounds.x + bounds.width <= parent.x + parent.width + 1 &&
                       bounds.y + bounds.height <= parent.y + parent.height + 1,
                   std::string(name) + " lies outside its declared area");
}

struct PixelEvidence {
  std::size_t matches{};
  std::size_t area{};
  int width{};
  int height{};
};

struct InkEvidence {
  std::size_t pixels{};
  std::size_t area{};
  std::size_t rows{};
  int horizontal_extent{};
  int width{};
  int height{};
};

inline auto measure_pixels(const ScreenshotImage &image, InputRectangle bounds,
                           InputPoint scale, UiPixelColor color,
                           int tolerance = 3, bool matching = true)
    -> PixelEvidence {
  require_property(image.width > 0 && image.height > 0 &&
                       image.rgba.size() ==
                           std::size_t(image.width) * image.height * 4,
                   "framebuffer dimensions do not match RGBA bytes");
  require_property(std::isfinite(scale.x) && std::isfinite(scale.y) &&
                       scale.x > 0 && scale.y > 0,
                   "drawable scale is invalid");
  require_bounds(bounds, {0, 0, image.width / scale.x, image.height / scale.y},
                 "pixel sample");
  const auto left = std::clamp(static_cast<int>(std::floor(bounds.x * scale.x)),
                               0, static_cast<int>(image.width));
  const auto top = std::clamp(static_cast<int>(std::floor(bounds.y * scale.y)),
                              0, static_cast<int>(image.height));
  const auto right = std::clamp(
      static_cast<int>(std::ceil((bounds.x + bounds.width) * scale.x)), left,
      static_cast<int>(image.width));
  const auto bottom = std::clamp(
      static_cast<int>(std::ceil((bounds.y + bounds.height) * scale.y)), top,
      static_cast<int>(image.height));
  auto evidence = PixelEvidence{};
  evidence.area = std::size_t(right - left) * (bottom - top);
  auto first_x = right;
  auto first_y = bottom;
  auto last_x = left - 1;
  auto last_y = top - 1;
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      const auto offset = (std::size_t(y) * image.width + x) * 4;
      auto close = true;
      for (int channel = 0; channel < 3; ++channel) {
        close = close && std::abs(int(image.rgba[offset + channel]) -
                                  color[channel]) <= tolerance;
      }
      if (close != matching) {
        continue;
      }
      ++evidence.matches;
      first_x = std::min(first_x, x);
      first_y = std::min(first_y, y);
      last_x = std::max(last_x, x);
      last_y = std::max(last_y, y);
    }
  }
  if (evidence.matches != 0) {
    evidence.width = last_x - first_x + 1;
    evidence.height = last_y - first_y + 1;
  }
  return evidence;
}

inline auto measure_ink(const ScreenshotImage &image, InputRectangle bounds,
                        InputPoint scale, UiPixelColor background,
                        int contrast = 40) -> InkEvidence {
  require_property(image.width > 0 && image.height > 0 &&
                       image.rgba.size() ==
                           std::size_t(image.width) * image.height * 4,
                   "framebuffer dimensions do not match RGBA bytes");
  require_bounds(bounds, {0, 0, image.width / scale.x, image.height / scale.y},
                 "text sample");
  const auto left = std::clamp(static_cast<int>(std::floor(bounds.x * scale.x)),
                               0, static_cast<int>(image.width));
  const auto top = std::clamp(static_cast<int>(std::floor(bounds.y * scale.y)),
                              0, static_cast<int>(image.height));
  const auto right = std::clamp(
      static_cast<int>(std::ceil((bounds.x + bounds.width) * scale.x)), left,
      static_cast<int>(image.width));
  const auto bottom = std::clamp(
      static_cast<int>(std::ceil((bounds.y + bounds.height) * scale.y)), top,
      static_cast<int>(image.height));
  auto evidence = InkEvidence{};
  evidence.area = std::size_t(right - left) * (bottom - top);
  auto first_x = right;
  auto last_x = left - 1;
  auto first_y = bottom;
  auto last_y = top - 1;
  const auto row_floor = std::max(1, (right - left) / 100);
  for (int y = top; y < bottom; ++y) {
    auto row_pixels = 0;
    for (int x = left; x < right; ++x) {
      const auto offset = (std::size_t(y) * image.width + x) * 4;
      const auto difference =
          std::max({std::abs(int(image.rgba[offset]) - background[0]),
                    std::abs(int(image.rgba[offset + 1]) - background[1]),
                    std::abs(int(image.rgba[offset + 2]) - background[2])});
      if (difference < contrast) {
        continue;
      }
      ++evidence.pixels;
      ++row_pixels;
      first_x = std::min(first_x, x);
      last_x = std::max(last_x, x);
      first_y = std::min(first_y, y);
      last_y = std::max(last_y, y);
    }
    if (row_pixels >= row_floor) {
      ++evidence.rows;
    }
  }
  if (evidence.pixels != 0) {
    evidence.horizontal_extent = last_x - first_x + 1;
    evidence.width = evidence.horizontal_extent;
    evidence.height = last_y - first_y + 1;
  }
  return evidence;
}

inline auto require_window_coverage(const ScreenshotImage &image,
                                    const UiFrameLayout &layout) -> double {
  require_bounds(layout.window, layout.window, "window");
  const auto scale = layout.drawable_scale;
  require_property(
      std::abs(layout.window.x) <= 1 && std::abs(layout.window.y) <= 1 &&
          std::abs(layout.window.width * scale.x - image.width) <= 1 &&
          std::abs(layout.window.height * scale.y - image.height) <= 1,
      "window geometry does not fill the drawable at its reported scale");
  const auto pixels =
      measure_pixels(image, layout.window, scale, layout.canvas, 3, false);
  require_property(pixels.matches != 0,
                   "shell is blank: no geometry differs from the clear color");
  const auto coverage = double(pixels.matches) / pixels.area;
  // The workspace must occupy a majority of the drawable. A half-scale frame
  // occupies at most one quarter even if every submitted pixel is opaque.
  require_property(coverage > 0.5,
                   "geometry does not fill the window: painted coverage=" +
                       std::to_string(coverage) +
                       " (requires >0.5; check drawable scale)");
  return coverage;
}

inline void require_surface(const ScreenshotImage &image,
                            const UiFrameLayout &layout, InputRectangle bounds,
                            std::string_view name) {
  require_bounds(bounds, layout.window, name);
  const auto pixels =
      measure_pixels(image, bounds, layout.drawable_scale, layout.surface);
  // Leave room for text, selection and decoration while rejecting empty or
  // incorrectly scaled surfaces. This is an area ratio, not a pixel count.
  require_property(pixels.matches > pixels.area * 0.4,
                   std::string(name) +
                       " geometry does not fill its declared area");
}

inline void require_text(const ScreenshotImage &image,
                         const UiFrameLayout &layout,
                         const UiTextGeometry &text, InputRectangle parent,
                         std::string_view name) {
  require_bounds(text.ink, parent, name);
  require_property(text.line_height > 0,
                   std::string(name) + " did not report a text line height");
  const auto ink =
      measure_ink(image, text.ink, layout.drawable_scale, layout.surface);
  const auto logical_lines = std::max(
      1, static_cast<int>(std::ceil(text.ink.height / text.line_height)));
  const auto expected_rows =
      std::max(3, static_cast<int>(std::ceil(logical_lines * text.line_height *
                                             layout.drawable_scale.y * 0.28F)));
  const auto fraction = double(ink.pixels) / ink.area;
  const auto minimum_extent = static_cast<int>(
      std::ceil(text.ink.width * layout.drawable_scale.x * 0.25F));
  // Ink is contrast against the reported surface, so syntax roles and
  // antialiasing remain text. Density, rows and span reject a dot, rule or
  // fill.
  require_property(
      fraction >= 0.015 && fraction <= 0.55 && ink.rows >= expected_rows &&
          ink.horizontal_extent >= minimum_extent,
      std::string(name) + " text distribution failed: fraction=" +
          std::to_string(fraction) + " rows=" + std::to_string(ink.rows) +
          " expected_rows=" + std::to_string(expected_rows) +
          " extent=" + std::to_string(ink.horizontal_extent) +
          " minimum_extent=" + std::to_string(minimum_extent) + " rect=" +
          std::to_string(static_cast<int>(
              std::ceil(text.ink.width * layout.drawable_scale.x))) +
          "x" +
          std::to_string(static_cast<int>(
              std::ceil(text.ink.height * layout.drawable_scale.y))));
}

inline void require_toolbar(const ScreenshotImage &image,
                            const UiFrameLayout &layout) {
  require_bounds(layout.toolbar, layout.window, "toolbar");
  require_text(image, layout, layout.toolbar_text, layout.toolbar,
               "shell controls");
}

inline void require_editor(const ScreenshotImage &image,
                           const UiFrameLayout &layout) {
  require_surface(image, layout, layout.editor, "editor");
  require_text(image, layout, layout.editor_text, layout.editor,
               "editor source");
}

inline void require_panel(const ScreenshotImage &image,
                          const UiFrameLayout &layout, PanelId id,
                          bool expect_error_text) {
  const auto found = layout.panels.find(id);
  require_property(found != layout.panels.end(),
                   "panel did not report its declared area");
  const auto &panel = found->second;
  require_surface(image, layout, panel.content, "panel");
  require_bounds(panel.tab, layout.window, "panel tab");
  require_property(panel.selected, "panel tab is not selected");
  const auto pixels =
      measure_pixels(image, panel.tab, layout.drawable_scale, layout.selected);
  require_property(
      pixels.matches > pixels.area * 0.25,
      "active panel tab highlight is missing in its reported bounds");
  if (expect_error_text) {
    require_text(image, layout, panel.text, panel.content, "diagnostic error");
  }
}

} // namespace liquify::test
