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
  EXPECT_TRUE(test::region_contains_color(image, {330, 570, 300, 18},
                                          {186, 27, 35, 255}, 40, 2));
}

} // namespace
} // namespace liquify
