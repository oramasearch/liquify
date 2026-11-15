#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "liquify/app.hpp"
#include "liquify/screenshot.hpp"
#include "ui_pixel_assert.hpp"
#include "ui_property_assert.hpp"

namespace liquify {
namespace {

auto render_state(std::string_view state) -> ScreenshotImage {
  const auto output = std::filesystem::path(LIQUIFY_TEST_OUTPUT_DIR) /
                      (std::string(state) + "-test.png");
  auto options = AppOptions{};
  options.screenshot_state = std::string(state);
  options.screenshot_output = output;
  options.max_frames = 2;
  auto app = App(std::move(options));
  if (!app.init()) {
    ADD_FAILURE() << "screenshot app initialization failed";
    return {};
  }
  EXPECT_EQ(app.run(), 0);
  auto image = read_png(output);
  if (!image) {
    ADD_FAILURE() << image.error();
    return {};
  }
  return std::move(*image);
}

struct DiagnosticsScreenshot {
  ScreenshotImage image;
  UiFrameLayout layout;
};

auto render_diagnostics(std::string_view state) -> DiagnosticsScreenshot {
  const auto output = std::filesystem::path(LIQUIFY_TEST_OUTPUT_DIR) /
                      (std::string(state) + "-test.png");
  auto options = AppOptions{};
  options.screenshot_state = std::string(state);
  options.screenshot_output = output;
  options.max_frames = 2;
  auto app = App(std::move(options));
  if (!app.init()) {
    ADD_FAILURE() << "screenshot app initialization failed";
    return {};
  }
  EXPECT_EQ(app.run(), 0);
  auto image = read_png(output);
  if (!image) {
    ADD_FAILURE() << image.error();
    return {};
  }
  return {std::move(*image), app.frame_layout()};
}

TEST(Screenshot, EditorRendersSelectedSource) {
  const auto image = render_state("editor-cpp-light");
  EXPECT_EQ(image.width, screenshot_width);
  EXPECT_EQ(image.height, screenshot_height);
  EXPECT_TRUE(test::region_contains_color(image, {88, 92, 224, 24},
                                          {237, 245, 255, 255}, 1, 4'000));
}

TEST(Screenshot, PanelRendersDiagnostics) {
  for (const auto state : {"diagnostics-light", "diagnostics-dark"}) {
    SCOPED_TRACE(state);
    const auto rendered = render_diagnostics(state);
    EXPECT_NO_THROW({
      test::require_window_coverage(rendered.image, rendered.layout);
      test::require_toolbar(rendered.image, rendered.layout);
      test::require_editor(rendered.image, rendered.layout);
      test::require_panel(rendered.image, rendered.layout, PanelId::diagnostics,
                          true);
    });
  }
}

TEST(Screenshot, IconsDoNotTouchTheirLabels) {
  for (const auto state : {"panels-light", "panels-dark"}) {
    SCOPED_TRACE(state);
    const auto rendered = render_diagnostics(state);
    const auto panel = rendered.layout.panels.find(PanelId::repository);
    ASSERT_NE(panel, rendered.layout.panels.end())
        << "the shell did not report the repository panel";
    EXPECT_NO_THROW({
      test::require_icons_clear_their_labels(
          rendered.image, panel->second.content, rendered.layout.drawable_scale,
          rendered.layout.surface, "repository panel");
    });
  }
}

TEST(Screenshot, CommandPaletteShowsSearchAndRecentCommands) {
  const auto image = render_state("command-palette-light");
  EXPECT_TRUE(test::region_contains_color(image, {260, 140, 760, 520},
                                          {255, 255, 255, 255}, 1, 100'000));
  EXPECT_TRUE(test::region_contains_color(image, {270, 180, 725, 34},
                                          {244, 244, 244, 255}, 1, 5'000));
}

TEST(Screenshot, EditorViewShowsSearchGutterAndBothThemes) {
  const auto light = render_state("editor-view-light");
  const auto dark = render_state("editor-view-dark");
  EXPECT_EQ(light.width, screenshot_width);
  EXPECT_EQ(dark.height, screenshot_height);
  EXPECT_TRUE(test::region_contains_color(light, {330, 184, 72, 360},
                                          {244, 244, 244, 255}, 2, 50));
  EXPECT_TRUE(test::region_contains_color(dark, {330, 184, 72, 360},
                                          {29, 29, 35, 255}, 2, 50));
}

TEST(Screenshot, RegistryIncludesPanelSystemInBothThemes) {
  EXPECT_NE(find_screenshot_state("panels-light"), nullptr);
  EXPECT_NE(find_screenshot_state("panels-dark"), nullptr);
  EXPECT_NE(find_screenshot_state("repository-unreadable-light"), nullptr);
  EXPECT_NE(find_screenshot_state("repository-unreadable-dark"), nullptr);
  EXPECT_NE(find_screenshot_state("lake-offline-light"), nullptr);
  EXPECT_NE(find_screenshot_state("lake-offline-dark"), nullptr);
  EXPECT_NE(find_screenshot_state("settings-light"), nullptr);
  EXPECT_NE(find_screenshot_state("settings-dark"), nullptr);
  EXPECT_NE(find_screenshot_state("accessibility-light"), nullptr);
  EXPECT_NE(find_screenshot_state("accessibility-dark"), nullptr);
  EXPECT_NE(find_screenshot_state("command-palette-light"), nullptr);
  EXPECT_NE(find_screenshot_state("command-palette-dark"), nullptr);
  EXPECT_EQ(screenshot_states().size(), 34U);
}

TEST(Screenshot, NavigationRendersQuickOpenAndAnnotatedTree) {
  const auto image = render_state("navigation-light");
  EXPECT_TRUE(test::region_contains_color(image, {320, 365, 650, 30},
                                          {237, 245, 255, 255}, 1, 10'000));
  EXPECT_TRUE(test::region_contains_color(image, {88, 92, 224, 24},
                                          {225, 230, 237, 255}, 1, 1'000));
}

TEST(Screenshot, SettingsRenderGroupedControlsInBothThemes) {
  const auto light = render_state("settings-light");
  const auto dark = render_state("settings-dark");

  EXPECT_EQ(light.width, screenshot_width);
  EXPECT_EQ(dark.height, screenshot_height);
  EXPECT_TRUE(test::region_contains_color(light, {190, 70, 900, 650},
                                          {255, 255, 255, 255}, 3, 100));
  EXPECT_TRUE(test::region_contains_color(dark, {190, 70, 900, 650},
                                          {18, 18, 22, 255}, 4, 100));
}

TEST(Screenshot, AccessibilityStateShowsHighContrastFocusInBothThemes) {
  const auto light = render_state("accessibility-light");
  const auto dark = render_state("accessibility-dark");

  EXPECT_TRUE(test::region_contains_color(light, {320, 100, 950, 660},
                                          {15, 98, 254, 255}, 1, 1'000));
  EXPECT_TRUE(test::region_contains_color(dark, {320, 100, 950, 660},
                                          {107, 152, 255, 255}, 1, 1'000));
}

} // namespace
} // namespace liquify

namespace liquify {
namespace {

auto relative_luminance(std::uint8_t r, std::uint8_t g, std::uint8_t b) -> double {
  const auto channel = [](std::uint8_t value) {
    const auto c = value / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

auto contrast(double a, double b) -> double {
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// The header row's background is its most common colour; its ink is the pixel that differs from
// it most. Rendered labels must read at WCAG AA (4.5:1) against the header (liquify#109).
auto header_contrast(const ScreenshotImage &image, const InputRectangle &rect) -> double {
  auto counts = std::map<std::uint32_t, int>{};
  const auto x0 = static_cast<int>(std::max(rect.x, 0.0F));
  const auto y0 = static_cast<int>(std::max(rect.y, 0.0F));
  const auto x1 = std::min(static_cast<int>(rect.x + rect.width), static_cast<int>(image.width));
  const auto y1 = std::min(static_cast<int>(rect.y + rect.height), static_cast<int>(image.height));
  const auto pixel = [&](int x, int y) {
    const auto *p = &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
    return std::array<std::uint8_t, 3>{p[0], p[1], p[2]};
  };
  for (int y = y0; y < y1 - 1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const auto [r, g, b] = pixel(x, y);
      ++counts[(static_cast<std::uint32_t>(r) << 16) | (g << 8) | b];
    }
  }
  if (counts.empty()) {
    return 0.0;
  }
  const auto background = std::ranges::max_element(counts, {}, [](const auto &entry) {
                            return entry.second;
                          })->first;
  const auto background_luminance =
      relative_luminance(background >> 16, (background >> 8) & 0xff, background & 0xff);
  auto best = 1.0;
  for (int y = y0; y < y1 - 1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const auto [r, g, b] = pixel(x, y);
      best = std::max(best, contrast(relative_luminance(r, g, b), background_luminance));
    }
  }
  return best;
}

TEST(Screenshot, TableHeadersReadAtAAContrastInBothThemes) {
  for (const auto &[state, control] :
       {std::pair{"command-palette-light", "palette/header"},
        std::pair{"command-palette-dark", "palette/header"},
        std::pair{"welcome-light", "recent-repositories/header"},
        std::pair{"welcome-dark", "recent-repositories/header"}}) {
    const auto rendered = render_diagnostics(state);
    ASSERT_TRUE(rendered.layout.controls.contains(control)) << state;
    const auto ratio = header_contrast(rendered.image, rendered.layout.controls.at(control));
    EXPECT_GE(ratio, 4.5) << state << " " << control;
  }
}

} // namespace
} // namespace liquify

namespace liquify {
namespace {

// liquify#100: titles, headings and body are different sizes, measured on rendered text rows.
TEST(Screenshot, TypeScaleSeparatesTitlesHeadingsAndBody) {
  for (const auto *state : {"welcome-light", "welcome-dark"}) {
    const auto rendered = render_diagnostics(state);
    const auto &controls = rendered.layout.controls;
    ASSERT_TRUE(controls.contains("welcome/title") && controls.contains("welcome/copy") &&
                controls.contains("welcome/recent-heading"))
        << state;
    const auto body = controls.at("welcome/copy").height;
    EXPECT_GE(controls.at("welcome/title").height, body * 1.5F) << state;
    EXPECT_GT(controls.at("welcome/recent-heading").height, body) << state;
  }
  for (const auto *state : {"repository-light", "repository-dark"}) {
    const auto rendered = render_diagnostics(state);
    const auto &controls = rendered.layout.controls;
    ASSERT_TRUE(controls.contains("panel/repository/title") && controls.contains("type/body"))
        << state;
    EXPECT_GT(controls.at("panel/repository/title").height, controls.at("type/body").height)
        << state;
  }
}

} // namespace
} // namespace liquify
