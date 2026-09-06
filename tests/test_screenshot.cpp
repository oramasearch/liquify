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
    const auto output =
        std::filesystem::path(LIQUIFY_TEST_OUTPUT_DIR) / (std::string(state) + "-test.png");
    auto options = AppOptions{};
    options.screenshot_state = std::string(state);
    options.screenshot_output = output;
    options.max_frames = 1;
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

TEST(Screenshot, EditorRendersHighlightedLine) {
    const auto image = render_state("editor-cpp-light");
    EXPECT_EQ(image.width, screenshot_width);
    EXPECT_EQ(image.height, screenshot_height);
    EXPECT_TRUE(
        test::region_contains_color(image, {250, 272, 1'020, 28}, {218, 224, 255, 255}, 1, 12'000));
}

TEST(Screenshot, PanelRendersTitle) {
    const auto image = render_state("diagnostics-light");
    EXPECT_TRUE(test::region_contains_color(image, {260, 580, 120, 40}, {30, 35, 42, 255}, 8, 20));
    EXPECT_TRUE(
        test::region_contains_color(image, {350, 580, 100, 40}, {95, 82, 214, 255}, 1, 300));
}

} // namespace
} // namespace liquify
