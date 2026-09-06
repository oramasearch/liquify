#include "liquify/png.hpp"

#include <array>
#include <fstream>
#include <vector>

#include <zlib.h>

namespace liquify {
namespace {

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_chunk(std::vector<std::uint8_t>& png,
                  const std::array<char, 4>& type,
                  std::span<const std::uint8_t> data) {
    append_u32(png, static_cast<std::uint32_t>(data.size()));
    const auto crc_start = png.size();
    for (const auto character : type) {
        png.push_back(static_cast<std::uint8_t>(character));
    }
    png.insert(png.end(), data.begin(), data.end());
    const auto checksum = crc32(0,
                                reinterpret_cast<const Bytef*>(png.data() + crc_start),
                                static_cast<uInt>(png.size() - crc_start));
    append_u32(png, static_cast<std::uint32_t>(checksum));
}

} // namespace

auto write_rgba_png(const std::filesystem::path& path,
                    int width,
                    int height,
                    int stride,
                    std::span<const std::uint8_t> pixels) -> std::string {
    if (width <= 0 || height <= 0 || stride < width * 4 ||
        pixels.size() < static_cast<std::size_t>(stride) * static_cast<std::size_t>(height)) {
        return "invalid RGBA image dimensions";
    }
    const auto row_bytes = static_cast<std::size_t>(width) * 4;
    auto filtered = std::vector<std::uint8_t>{};
    filtered.reserve((row_bytes + 1) * static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row) {
        filtered.push_back(0);
        const auto offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(stride);
        filtered.insert(filtered.end(),
                        pixels.begin() + offset,
                        pixels.begin() + offset + row_bytes);
    }
    auto compressed_size = compressBound(static_cast<uLong>(filtered.size()));
    auto compressed = std::vector<std::uint8_t>(compressed_size);
    const auto result = compress2(compressed.data(),
                                  &compressed_size,
                                  filtered.data(),
                                  static_cast<uLong>(filtered.size()),
                                  Z_BEST_COMPRESSION);
    if (result != Z_OK) {
        return "zlib could not compress PNG pixels";
    }
    compressed.resize(compressed_size);

    auto png = std::vector<std::uint8_t>{137, 80, 78, 71, 13, 10, 26, 10};
    auto header = std::vector<std::uint8_t>{};
    append_u32(header, static_cast<std::uint32_t>(width));
    append_u32(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_chunk(png, {'I', 'H', 'D', 'R'}, header);
    append_chunk(png, {'I', 'D', 'A', 'T'}, compressed);
    append_chunk(png, {'I', 'E', 'N', 'D'}, {});

    std::error_code filesystem_error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), filesystem_error);
    }
    if (filesystem_error) {
        return "cannot create screenshot directory: " + filesystem_error.message();
    }
    auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return "cannot write " + path.string();
    }
    output.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
    if (!output) {
        return "cannot write " + path.string();
    }
    return {};
}

} // namespace liquify
