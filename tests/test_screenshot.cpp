#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "liquify/app.hpp"
#include "liquify/screenshot.hpp"
#include "ui_pixel_assert.hpp"

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

TEST(Screenshot, EditorRendersSelectedSource) {
  const auto image = render_state("editor-cpp-light");
  EXPECT_EQ(image.width, screenshot_width);
  EXPECT_EQ(image.height, screenshot_height);
  EXPECT_TRUE(test::region_contains_color(image, {88, 92, 224, 24},
                                          {237, 245, 255, 255}, 1, 4'000));
}

TEST(Screenshot, PanelRendersDiagnostics) {
  const auto image = render_state("diagnostics-light");
  EXPECT_TRUE(test::region_contains_color(image, {330, 503, 930, 34},
                                          {237, 245, 255, 255}, 1, 10'000));
  EXPECT_TRUE(test::region_contains_color(image, {330, 590, 450, 30},
                                          {186, 27, 35, 255}, 40, 2));
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
