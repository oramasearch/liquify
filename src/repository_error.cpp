// SPDX-License-Identifier: MIT

#include "repository_error.hpp"

#include <array>
#include <format>
#include <fstream>
#include <optional>
#include <string_view>

#include "liquify/version.hpp"

namespace liquify {
namespace {

auto unsupported_object_map(const liquid::error& error) -> std::optional<unsigned> {
    if (error.code != liquid::errc::corrupt || error.message != "invalid object map") {
        return std::nullopt;
    }
    // Inspect only the format tag after the library has refused the file. Records,
    // index recovery, and all repository I/O remain owned by libliquid.
    auto input = std::ifstream(error.context, std::ios::binary);
    auto header = std::array<char, 12>{};
    if (!input.read(header.data(), header.size()) ||
        std::string_view(header.data(), 5) != "LQOBJ" || header[6] != 0 || header[7] != 0) {
        return std::nullopt;
    }
    const auto version = static_cast<unsigned char>(header[5]);
    if (version >= 1 && version <= 5) {
        return std::nullopt;
    }
    return version;
}

auto upgrade_message(std::string_view format) -> std::string {
    return std::format(
        "Unsupported liquid store format: {}. This build is {}. "
        "The store does not record its writer's artifact revision. Upgrade liquify and the "
        "liquid CLI together to a revision that supports this format; see "
        "docs/build/store-compatibility.md and the README compatibility table. "
        "Keep the original repository; use the writer's compatible CLI to export or clone "
        "into a separate directory if upgrading is unavailable.",
        format,
        build_description());
}

} // namespace

std::string repository_error_message(const liquid::error& error) {
    if (const auto version = unsupported_object_map(error)) {
        return error.context + ": " + upgrade_message(std::format("object map v{}", *version));
    }
    if (error.code == liquid::errc::unsupported && error.message == "unknown liquid store format") {
        return error.context + ": " +
               upgrade_message("unrecognized LIQUID_STORE marker (expected 1)");
    }
    if (error.context.empty()) {
        return error.message;
    }
    return error.context + ": " + error.message;
}

} // namespace liquify
