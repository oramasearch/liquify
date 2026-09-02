#include <gtest/gtest.h>

#include "liquify/version.hpp"

TEST(Version, StringMatchesStruct) {
    constexpr auto v = liquify::current_version();
    EXPECT_EQ(v.major, 0u);
    EXPECT_EQ(v.minor, 0u);
    EXPECT_EQ(v.patch, 1u);
    EXPECT_EQ(liquify::version_string(), "0.0.1");
}
