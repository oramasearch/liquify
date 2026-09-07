#include "liquify/screenshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>

#include <zlib.h>

namespace liquify {
namespace {

constexpr auto states = std::to_array<ScreenshotState>({
    {"welcome-light", ScreenshotView::welcome, ScreenshotTheme::light},
    {"welcome-dark", ScreenshotView::welcome, ScreenshotTheme::dark},
    {"repository-unreadable-light", ScreenshotView::repository_unreadable, ScreenshotTheme::light},
    {"repository-unreadable-dark", ScreenshotView::repository_unreadable, ScreenshotTheme::dark},
    {"lake-offline-light", ScreenshotView::lake_offline, ScreenshotTheme::light},
    {"lake-offline-dark", ScreenshotView::lake_offline, ScreenshotTheme::dark},
    {"repository-light", ScreenshotView::repository, ScreenshotTheme::light},
    {"repository-dark", ScreenshotView::repository, ScreenshotTheme::dark},
    {"editor-cpp-light", ScreenshotView::editor_cpp, ScreenshotTheme::light},
    {"editor-cpp-dark", ScreenshotView::editor_cpp, ScreenshotTheme::dark},
    {"editor-view-light", ScreenshotView::editor_view, ScreenshotTheme::light},
    {"editor-view-dark", ScreenshotView::editor_view, ScreenshotTheme::dark},
    {"diagnostics-light", ScreenshotView::diagnostics, ScreenshotTheme::light},
    {"diagnostics-dark", ScreenshotView::diagnostics, ScreenshotTheme::dark},
    {"build-light", ScreenshotView::build, ScreenshotTheme::light},
    {"build-dark", ScreenshotView::build, ScreenshotTheme::dark},
    {"review-light", ScreenshotView::review, ScreenshotTheme::light},
    {"review-dark", ScreenshotView::review, ScreenshotTheme::dark},
    {"assistant-light", ScreenshotView::assistant, ScreenshotTheme::light},
    {"assistant-dark", ScreenshotView::assistant, ScreenshotTheme::dark},
    {"plugins-light", ScreenshotView::plugins, ScreenshotTheme::light},
    {"plugins-dark", ScreenshotView::plugins, ScreenshotTheme::dark},
    {"command-palette-light", ScreenshotView::command_palette,
     ScreenshotTheme::light},
    {"command-palette-dark", ScreenshotView::command_palette,
     ScreenshotTheme::dark},
    {"structural-editing-light", ScreenshotView::structural_editing,
     ScreenshotTheme::light},
    {"structural-editing-dark", ScreenshotView::structural_editing,
     ScreenshotTheme::dark},
    {"panels-light", ScreenshotView::panels, ScreenshotTheme::light},
    {"panels-dark", ScreenshotView::panels, ScreenshotTheme::dark},
    {"settings-light", ScreenshotView::settings, ScreenshotTheme::light},
    {"settings-dark", ScreenshotView::settings, ScreenshotTheme::dark},
});

constexpr std::array<std::uint8_t, 8> png_signature{
    137, 80, 78, 71, 13, 10, 26, 10,
};

void append_u32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

auto read_u32(std::span<const std::uint8_t> bytes, std::size_t offset)
    -> std::uint32_t {
  return static_cast<std::uint32_t>(bytes[offset]) << 24 |
         static_cast<std::uint32_t>(bytes[offset + 1]) << 16 |
         static_cast<std::uint32_t>(bytes[offset + 2]) << 8 |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

auto crc32(std::span<const std::uint8_t> bytes) -> std::uint32_t {
  auto crc = std::uint32_t{0xffffffffU};
  for (const auto byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const auto mask = 0U - (crc & 1U);
      crc = crc >> 1U ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

void append_chunk(std::vector<std::uint8_t> &png,
                  const std::array<std::uint8_t, 4> &type,
                  std::span<const std::uint8_t> data) {
  append_u32(png, static_cast<std::uint32_t>(data.size()));
  const auto crc_start = png.size();
  png.insert(png.end(), type.begin(), type.end());
  png.insert(png.end(), data.begin(), data.end());
  append_u32(png, crc32(std::span(png).subspan(crc_start)));
}

auto encode_scanlines(const ScreenshotImage &image)
    -> std::vector<std::uint8_t> {
  const auto row_bytes = static_cast<std::size_t>(image.width) * 4;
  auto scanlines = std::vector<std::uint8_t>{};
  scanlines.reserve((row_bytes + 1) * image.height);
  for (std::uint32_t row = 0; row < image.height; ++row) {
    scanlines.push_back(0);
    const auto begin =
        image.rgba.begin() + static_cast<std::ptrdiff_t>(row * row_bytes);
    scanlines.insert(scanlines.end(), begin,
                     begin + static_cast<std::ptrdiff_t>(row_bytes));
  }
  return scanlines;
}

auto encode_deflate(std::span<const std::uint8_t> input)
    -> std::expected<std::vector<std::uint8_t>, std::string> {
  auto output_size = compressBound(static_cast<uLong>(input.size()));
  auto output = std::vector<std::uint8_t>(output_size);
  const auto result =
      compress2(output.data(), &output_size, input.data(),
                static_cast<uLong>(input.size()), Z_BEST_COMPRESSION);
  if (result != Z_OK) {
    return std::unexpected("cannot compress PNG image data");
  }
  output.resize(output_size);
  return output;
}

auto decode_deflate(std::span<const std::uint8_t> input,
                    std::size_t expected_size)
    -> std::expected<std::vector<std::uint8_t>, std::string> {
  auto output_size = static_cast<uLongf>(expected_size);
  auto output = std::vector<std::uint8_t>(expected_size);
  const auto result = uncompress(output.data(), &output_size, input.data(),
                                 static_cast<uLong>(input.size()));
  if (result != Z_OK || output_size != expected_size) {
    return std::unexpected("cannot decompress PNG image data");
  }
  return output;
}

auto parse_png(std::span<const std::uint8_t> png)
    -> std::expected<ScreenshotImage, std::string> {
  if (png.size() < png_signature.size() ||
      !std::equal(png_signature.begin(), png_signature.end(), png.begin())) {
    return std::unexpected("invalid PNG signature");
  }
  auto image = ScreenshotImage{};
  auto compressed = std::vector<std::uint8_t>{};
  auto offset = png_signature.size();
  while (offset + 12 <= png.size()) {
    const auto length = read_u32(png, offset);
    offset += 4;
    if (offset + length + 8 > png.size()) {
      return std::unexpected("truncated PNG chunk");
    }
    const auto type = std::string_view(
        reinterpret_cast<const char *>(png.data() + offset), 4);
    const auto data = png.subspan(offset + 4, length);
    const auto chunk = png.subspan(offset, length + 4);
    if (read_u32(png, offset + length + 4) != crc32(chunk)) {
      return std::unexpected("invalid PNG chunk checksum");
    }
    if (type == "IHDR") {
      if (length != 13 || data[8] != 8 || data[9] != 6 || data[10] != 0 ||
          data[11] != 0 || data[12] != 0) {
        return std::unexpected("unsupported PNG image format");
      }
      image.width = read_u32(data, 0);
      image.height = read_u32(data, 4);
    } else if (type == "IDAT") {
      compressed.insert(compressed.end(), data.begin(), data.end());
    } else if (type == "IEND") {
      break;
    }
    offset += length + 8;
  }
  if (image.width == 0 || image.height == 0 || compressed.empty()) {
    return std::unexpected("PNG is missing image data");
  }
  const auto row_bytes = static_cast<std::size_t>(image.width) * 4;
  auto scanlines = decode_deflate(compressed, (row_bytes + 1) * image.height);
  if (!scanlines) {
    return std::unexpected(scanlines.error());
  }
  if (scanlines->size() != (row_bytes + 1) * image.height) {
    return std::unexpected("PNG scanline size does not match its dimensions");
  }
  image.rgba.reserve(row_bytes * image.height);
  for (std::uint32_t row = 0; row < image.height; ++row) {
    const auto begin = static_cast<std::size_t>(row) * (row_bytes + 1);
    if ((*scanlines)[begin] != 0) {
      return std::unexpected("PNG uses an unsupported scanline filter");
    }
    image.rgba.insert(image.rgba.end(),
                      scanlines->begin() +
                          static_cast<std::ptrdiff_t>(begin + 1),
                      scanlines->begin() +
                          static_cast<std::ptrdiff_t>(begin + row_bytes + 1));
  }
  return image;
}

} // namespace

auto screenshot_states() -> std::span<const ScreenshotState> { return states; }

auto find_screenshot_state(std::string_view name) -> const ScreenshotState * {
  const auto found = std::ranges::find(states, name, &ScreenshotState::name);
  return found == states.end() ? nullptr : &*found;
}

auto write_png(const std::filesystem::path &path, const ScreenshotImage &image)
    -> std::expected<void, std::string> {
  if (image.width == 0 || image.height == 0 ||
      image.rgba.size() !=
          static_cast<std::size_t>(image.width) * image.height * 4) {
    return std::unexpected("invalid RGBA image dimensions");
  }
  auto ihdr = std::vector<std::uint8_t>{};
  append_u32(ihdr, image.width);
  append_u32(ihdr, image.height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  const auto scanlines = encode_scanlines(image);
  const auto compressed = encode_deflate(scanlines);
  if (!compressed) {
    return std::unexpected(compressed.error());
  }
  auto png =
      std::vector<std::uint8_t>(png_signature.begin(), png_signature.end());
  append_chunk(png, {'I', 'H', 'D', 'R'}, ihdr);
  append_chunk(png, {'I', 'D', 'A', 'T'}, *compressed);
  append_chunk(png, {'I', 'E', 'N', 'D'}, {});
  auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return std::unexpected("cannot open PNG output: " + path.string());
  }
  output.write(reinterpret_cast<const char *>(png.data()),
               static_cast<std::streamsize>(png.size()));
  if (!output) {
    return std::unexpected("cannot write PNG output: " + path.string());
  }
  return {};
}

auto read_png(const std::filesystem::path &path)
    -> std::expected<ScreenshotImage, std::string> {
  auto input = std::ifstream(path, std::ios::binary);
  if (!input) {
    return std::unexpected("cannot open PNG input: " + path.string());
  }
  const auto characters =
      std::vector<char>(std::istreambuf_iterator<char>(input), {});
  const auto bytes = std::as_bytes(std::span(characters));
  return parse_png(
      {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()});
}

} // namespace liquify
