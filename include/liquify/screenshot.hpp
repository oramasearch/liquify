#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace liquify {

inline constexpr std::uint32_t screenshot_width = 1280;
inline constexpr std::uint32_t screenshot_height = 800;

enum class ScreenshotTheme : std::uint8_t {
  light,
  dark,
};

enum class ScreenshotView : std::uint8_t {
  welcome,
  repository,
  editor_cpp,
  diagnostics,
  build,
  review,
  assistant,
  plugins,
  command_palette,
  structural_editing,
};

struct ScreenshotState {
  std::string_view name;
  ScreenshotView view;
  ScreenshotTheme theme;
};

struct ScreenshotImage {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::uint8_t> rgba;
};

[[nodiscard]] auto screenshot_states() -> std::span<const ScreenshotState>;
[[nodiscard]] auto find_screenshot_state(std::string_view name)
    -> const ScreenshotState *;
[[nodiscard]] auto write_png(const std::filesystem::path &path,
                             const ScreenshotImage &image)
    -> std::expected<void, std::string>;
[[nodiscard]] auto read_png(const std::filesystem::path &path)
    -> std::expected<ScreenshotImage, std::string>;

} // namespace liquify
