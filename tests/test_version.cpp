// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "liquify/version.hpp"

TEST(Version, StringMatchesStruct) {
    constexpr auto v = liquify::current_version();
    EXPECT_EQ(v.major, 0u);
    EXPECT_EQ(v.minor, 0u);
    EXPECT_EQ(v.patch, 1u);
    EXPECT_TRUE(liquify::version_string().starts_with("0.0.1+"));
    EXPECT_EQ(liquify::liquid_revision().size(), 40U);
    EXPECT_NE(liquify::build_description().find(liquify::liquid_revision()), std::string::npos);
}
