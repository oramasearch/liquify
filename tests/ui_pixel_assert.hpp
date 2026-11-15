// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "liquify/screenshot.hpp"

namespace liquify::test {

struct PixelRegion {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
};

struct Rgba {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};
  std::uint8_t alpha{255};
};

[[nodiscard]] inline auto
region_contains_color(const ScreenshotImage &image, PixelRegion region,
                      Rgba expected, std::uint8_t tolerance,
                      std::size_t minimum_pixels) -> testing::AssertionResult {
  const auto expected_size =
      static_cast<std::size_t>(image.width) * image.height * 4;
  if (image.rgba.size() != expected_size) {
    return testing::AssertionFailure()
           << "RGBA byte count does not match image dimensions";
  }
  if (region.x > image.width || region.y > image.height ||
      region.width > image.width - region.x ||
      region.height > image.height - region.y) {
    return testing::AssertionFailure() << "pixel region exceeds " << image.width
                                       << 'x' << image.height << " image";
  }
  const auto close = [tolerance](std::uint8_t actual, std::uint8_t target) {
    const auto difference = std::max(actual, target) - std::min(actual, target);
    return difference <= tolerance;
  };
  auto matches = std::size_t{};
  for (std::uint32_t y = region.y; y < region.y + region.height; ++y) {
    for (std::uint32_t x = region.x; x < region.x + region.width; ++x) {
      const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
      if (close(image.rgba[offset], expected.red) &&
          close(image.rgba[offset + 1], expected.green) &&
          close(image.rgba[offset + 2], expected.blue) &&
          close(image.rgba[offset + 3], expected.alpha)) {
        ++matches;
      }
    }
  }
  if (matches < minimum_pixels) {
    return testing::AssertionFailure()
           << "pixel region contained " << matches
           << " matching pixels; expected at least " << minimum_pixels;
  }
  return testing::AssertionSuccess();
}

} // namespace liquify::test
