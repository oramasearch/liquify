#include <gtest/gtest.h>

#include "../src/text_layout.hpp"

TEST(TextLayoutCache, RetainsUnchangedLinesAndInvalidatesOnlyTheEdit) {
    liquify::TextLayoutCache cache;
    cache.reset("blob-1", 3);
    const auto first_generation = cache.line(0, "first").generation;
    const auto second_generation = cache.line(1, "second").generation;
    const auto third_generation = cache.line(2, "third").generation;
    EXPECT_EQ(cache.build_count(), 3U);

    EXPECT_EQ(cache.line(1, "second").generation, second_generation);
    EXPECT_EQ(cache.build_count(), 3U);

    cache.apply_change({1, 1, 1});
    EXPECT_EQ(cache.line(0, "first").generation, first_generation);
    EXPECT_NE(cache.line(1, "changed").generation, second_generation);
    EXPECT_EQ(cache.line(2, "third").generation, third_generation);
    EXPECT_EQ(cache.build_count(), 4U);
}

TEST(TextLayoutCache, PreservesFollowingLinesWhenAnEditAddsALine) {
    liquify::TextLayoutCache cache;
    cache.reset("blob-2", 2);
    const auto trailing_generation = cache.line(1, "trailing").generation;

    cache.apply_change({0, 1, 2});
    EXPECT_EQ(cache.line_count(), 3U);
    EXPECT_FALSE(cache.line(0, "split").glyphs.empty());
    EXPECT_FALSE(cache.line(1, "line").glyphs.empty());
    EXPECT_EQ(cache.line(2, "trailing").generation, trailing_generation);
}

TEST(TextLayoutCache, ExpandsTabsAndIndexesUtf8ByByte) {
    liquify::TextLayoutCache cache;
    cache.reset("blob-3", 1);
    const std::string utf8{'a', '\t', static_cast<char>(0xc3), static_cast<char>(0xa9)};
    const auto& layout = cache.line(0, utf8);
    ASSERT_EQ(layout.glyphs.size(), 3U);
    EXPECT_EQ(layout.glyphs[2].byte, 2U);
    EXPECT_FLOAT_EQ(layout.glyphs[2].column, 4.0F);
    EXPECT_FLOAT_EQ(layout.columns, 5.0F);
    EXPECT_FLOAT_EQ(layout.column_for_byte(2), 4.0F);
}
