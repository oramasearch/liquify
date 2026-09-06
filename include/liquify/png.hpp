#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace liquify {

[[nodiscard]] auto write_rgba_png(const std::filesystem::path& path,
                                  int width,
                                  int height,
                                  int stride,
                                  std::span<const std::uint8_t> pixels) -> std::string;

} // namespace liquify
