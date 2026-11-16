// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <format>
#include <string>

#include "liquify/version.hpp"

TEST(Version, StringMatchesStruct) {
    // The version comes from CMake's project(VERSION); nothing here repeats the number.
    constexpr auto v = liquify::current_version();
    const auto numbers = std::format("{}.{}.{}", v.major, v.minor, v.patch);
    EXPECT_EQ(numbers, LIQUIFY_VERSION_STRING);
    EXPECT_TRUE(liquify::version_string().starts_with(numbers + "+"));
    EXPECT_EQ(liquify::liquid_revision().size(), 40U);
    EXPECT_NE(liquify::build_description().find(liquify::liquid_revision()), std::string::npos);
}
