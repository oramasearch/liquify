#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "liquify/png.hpp"

namespace {

TEST(Png, WritesRgbaPixelsWithTheRequestedDimensions) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path =
        std::filesystem::temp_directory_path() / ("liquify-png-" + std::to_string(suffix) + ".png");
    const auto pixels = std::array<std::uint8_t, 16>{
        255,
        0,
        0,
        255,
        0,
        255,
        0,
        255,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
    };

    ASSERT_TRUE(liquify::write_rgba_png(path, 2, 2, 8, pixels).empty());
    auto input = std::ifstream(path, std::ios::binary);
    const auto bytes = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    ASSERT_GE(bytes.size(), 24U);
    EXPECT_EQ((std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 8)),
              (std::vector<std::uint8_t>{137, 80, 78, 71, 13, 10, 26, 10}));
    EXPECT_EQ(bytes[19], 2U);
    EXPECT_EQ(bytes[23], 2U);
}

} // namespace
