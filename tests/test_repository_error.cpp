// SPDX-License-Identifier: MIT

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "../src/repository_error.hpp"
#include "liquid/version.hpp"

namespace {

TEST(RepositoryError, KeepsCorruptionAndOtherErrorsDistinctFromIncompatibility) {
    const auto error = liquid::error{liquid::errc::corrupt, "invalid object map", "absent.meta"};
    EXPECT_EQ(liquify::repository_error_message(error), "absent.meta: invalid object map");
    EXPECT_EQ(liquify::repository_error_message({liquid::errc::io, "permission denied", "file"}),
              "file: permission denied");
}

TEST(RepositoryError, RecognizesOnlyCompleteUnknownObjectMapTags) {
    const auto file = std::filesystem::path(LIQUIFY_TEST_OUTPUT_DIR) / "compat-object-map.meta";
    const auto error = liquid::error{liquid::errc::corrupt, "invalid object map", file.string()};
    auto header = std::array<char, 12>{'L', 'Q', 'O', 'B', 'J', 6, 0, 0, 0, 0, 0, 0};
    {
        auto output = std::ofstream(file, std::ios::binary);
        output.write(header.data(), header.size());
    }
    const auto message = liquify::repository_error_message(error);
    EXPECT_NE(message.find("object map v6"), std::string::npos);
    EXPECT_NE(message.find("Upgrade liquify and the liquid CLI together"), std::string::npos);
    // The guidance names the libliquid this binary links, read from libliquid itself.
    EXPECT_NE(message.find("libliquid " + std::string(liquid::version_string()) + "+"),
              std::string::npos);
    EXPECT_EQ(message.find("invalid object map"), std::string::npos);
    for (const auto version : {1, 2, 3, 4, 5}) {
        header[5] = static_cast<char>(version);
        {
            auto output = std::ofstream(file, std::ios::binary);
            output.write(header.data(), header.size());
        }
        EXPECT_EQ(liquify::repository_error_message(error), file.string() + ": invalid object map");
    }
    {
        auto output = std::ofstream(file, std::ios::binary);
        output.write(header.data(), 6);
    }
    EXPECT_EQ(liquify::repository_error_message(error), file.string() + ": invalid object map");
    std::filesystem::remove(file);
}

TEST(RepositoryError, AddsUpgradeGuidanceToUnsupportedStoreMarkers) {
    const auto message = liquify::repository_error_message(
        {liquid::errc::unsupported, "unknown liquid store format", "LIQUID_STORE"});
    EXPECT_NE(message.find("expected 1"), std::string::npos);
    EXPECT_NE(message.find("Upgrade liquify"), std::string::npos);
}

} // namespace
