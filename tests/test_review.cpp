#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#include "forge_double.hpp"
#include "liquid/forge.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/review.hpp"

namespace {

class TemporaryRepository {
public:
    TemporaryRepository()
        : path_(std::filesystem::temp_directory_path() /
                ("liquify-review-" + process_suffix() + "-" + std::to_string(next_++))) {
        auto ignored = std::error_code{};
        std::filesystem::remove_all(path_, ignored);
    }

    ~TemporaryRepository() {
        auto ignored = std::error_code{};
        std::filesystem::remove_all(path_, ignored);
    }

    auto path() const -> const std::filesystem::path& {
        return path_;
    }

private:
    // ctest runs each test in its own process, where the counter restarts at zero; a
    // per-process random suffix keeps parallel tests out of each other's directories.
    static auto process_suffix() -> const std::string& {
        static const auto suffix = [] {
            auto device = std::random_device{};
            return std::format("{:08x}{:08x}", device(), device());
        }();
        return suffix;
    }

    inline static std::atomic_uint64_t next_{};
    std::filesystem::path path_;
};

auto create_local_repository(const std::filesystem::path& path) -> liquid::result<void> {
    auto repository = liquid::runtime::block_on(liquid::repository::create(path));
    if (!repository) {
        return std::unexpected(repository.error());
    }
    return {};
}

auto open_review(liquid::test::forge_double& server,
                 std::span<const std::pair<std::string_view, std::string_view>> files)
    -> liquid::result<liquid::forge_review> {
    auto head = liquid::result<liquid::change_id>{};
    for (const auto& [path, contents] : files) {
        head = server.advance_head(std::string{path}, std::string{contents});
        if (!head) {
            return std::unexpected(head.error());
        }
    }
    auto client = liquid::forge_client{server.url()};
    return liquid::runtime::block_on(client.create_review(*head));
}

} // namespace

TEST(ReviewModel, opens_a_pulled_change_and_creates_a_cursor_comment) {
    auto server = liquid::test::forge_double{};
    const auto files = std::to_array<std::pair<std::string_view, std::string_view>>({
        {"src/file.cpp", "one\ntwo changed\nthree\n"},
    });
    auto review = open_review(server, files);
    ASSERT_TRUE(review) << review.error().message;

    TemporaryRepository local;
    ASSERT_TRUE(create_local_repository(local.path()));
    auto model = liquify::ReviewModel{local.path(), server.url()};
    auto listed = liquid::runtime::block_on(model.list_reviews());
    ASSERT_TRUE(listed) << listed.error().message;
    ASSERT_EQ(listed->reviews.size(), 1U);

    auto opened = liquid::runtime::block_on(model.open_review(review->id));
    ASSERT_TRUE(opened) << opened.error().message << " (" << opened.error().context << ')';
    ASSERT_FALSE(opened->diff.empty());
    EXPECT_EQ(opened->diff.front().kind, liquify::ReviewDiffLineKind::file);
    EXPECT_EQ(opened->diff.front().text, "text diff -- src/file.cpp");

    auto commented =
        liquid::runtime::block_on(model.create_comment("cursor comment", "src/file.cpp", 2, 4));
    ASSERT_TRUE(commented) << commented.error().message;
    ASSERT_EQ(commented->comments.size(), 1U);
    EXPECT_EQ(commented->comments.front().value.body, "cursor comment");
    EXPECT_EQ(commented->comments.front().value.anchor.line, 2U);
    EXPECT_EQ(commented->comments.front().value.anchor.column, 4U);
}

TEST(ReviewModel, refresh_maps_shifted_and_deleted_comments_and_updates_state) {
    auto server = liquid::test::forge_double{};
    const auto files = std::to_array<std::pair<std::string_view, std::string_view>>({
        {"moved.txt", "one\nmove me\nthree\n"},
        {"shifted.txt", "one\nkeep me\nthree\n"},
        {"deleted.txt", "one\ndelete me\nthree\n"},
    });
    auto review = open_review(server, files);
    ASSERT_TRUE(review) << review.error().message;

    TemporaryRepository local;
    ASSERT_TRUE(create_local_repository(local.path()));
    auto model = liquify::ReviewModel{local.path(), server.url()};
    auto opened = liquid::runtime::block_on(model.open_review(review->id));
    ASSERT_TRUE(opened) << opened.error().message;
    ASSERT_TRUE(liquid::runtime::block_on(model.create_comment("moved", "moved.txt", 2, 1)));
    ASSERT_TRUE(liquid::runtime::block_on(model.create_comment("shifted", "shifted.txt", 2, 1)));
    ASSERT_TRUE(liquid::runtime::block_on(model.create_comment("deleted", "deleted.txt", 2, 1)));

    ASSERT_TRUE(server.advance_head("moved.txt", "inserted\none\nmove me\nthree\n"));
    ASSERT_TRUE(server.advance_head("shifted.txt", "inserted\none\nkeep me\nthree\n"));
    ASSERT_TRUE(server.advance_head("deleted.txt", "one\nthree\n"));

    auto refreshed = liquid::runtime::block_on(model.refresh());
    ASSERT_TRUE(refreshed) << refreshed.error().message << " (" << refreshed.error().context << ')';
    ASSERT_EQ(refreshed->comments.size(), 3U);
    const auto moved = std::ranges::find(
        refreshed->comments, "moved", [](const auto& comment) { return comment.value.body; });
    const auto shifted = std::ranges::find(
        refreshed->comments, "shifted", [](const auto& comment) { return comment.value.body; });
    const auto deleted = std::ranges::find(
        refreshed->comments, "deleted", [](const auto& comment) { return comment.value.body; });
    ASSERT_NE(moved, refreshed->comments.end());
    ASSERT_NE(shifted, refreshed->comments.end());
    ASSERT_NE(deleted, refreshed->comments.end());
    EXPECT_EQ(moved->state, liquify::ReviewAnchorState::moved);
    EXPECT_EQ(moved->value.anchor.line, 3U);
    EXPECT_EQ(shifted->state, liquify::ReviewAnchorState::moved);
    EXPECT_EQ(shifted->value.anchor.line, 3U);
    EXPECT_EQ(deleted->state, liquify::ReviewAnchorState::deleted);

    auto approved = liquid::runtime::block_on(model.approve());
    ASSERT_TRUE(approved) << approved.error().message;
    EXPECT_EQ(approved->selected->state, liquid::forge_review_state::approved);
    auto requested = liquid::runtime::block_on(model.request_changes());
    ASSERT_TRUE(requested) << requested.error().message;
    EXPECT_EQ(requested->selected->state, liquid::forge_review_state::open);
}

#include "liquid/drive.hpp"
#include "liquid/sync.hpp"

namespace {

TEST(ReviewModel, requests_a_review_of_the_pushed_current_change) {
    auto server = liquid::test::forge_double{};
    ASSERT_TRUE(server.advance_head("src/file.cpp", "one\n"));
    TemporaryRepository local;
    {
        auto client = liquid::runtime::block_on(liquid::drive_client::open(local.path(), server.url()));
        ASSERT_TRUE(client) << client.error().message;
        ASSERT_TRUE(liquid::runtime::block_on(client->pin("")));
        ASSERT_TRUE(liquid::runtime::block_on(client->sync()));
    }
    auto head = liquid::change_id{};
    {
        auto repository = liquid::runtime::block_on(liquid::repository::open(local.path()));
        ASSERT_TRUE(repository);
        const auto text = std::string_view{"one\ntwo\n"};
        const auto bytes = std::as_bytes(std::span(text));
        ASSERT_TRUE(liquid::runtime::block_on(
            repository->write("src/file.cpp", {bytes.begin(), bytes.end()})));
        auto cut = liquid::runtime::block_on(repository->cut("Add two"));
        ASSERT_TRUE(cut) << cut.error().message;
        head = *cut;
        auto endpoint = liquid::sync_endpoint{server.url()};
        auto pushed = liquid::runtime::block_on(endpoint.push(*repository));
        ASSERT_TRUE(pushed) << pushed.error().message;
    }
    auto model = liquify::ReviewModel{local.path(), server.url()};
    auto requested = liquid::runtime::block_on(model.request_review({"ana", "bo"}));
    ASSERT_TRUE(requested) << requested.error().message;
    ASSERT_TRUE(requested->selected);
    EXPECT_EQ(liquid::to_string(requested->selected->change.value), liquid::to_string(head.value));
    EXPECT_EQ(requested->selected->reviewers, (std::vector<std::string>{"ana", "bo"}));
    EXPECT_EQ(requested->reviews.size(), 1U);
}

TEST(ReviewModel, parses_optional_reviewers) {
    EXPECT_EQ(liquify::parse_reviewers(" ana, bo ,, cy "),
              (std::vector<std::string>{"ana", "bo", "cy"}));
    EXPECT_TRUE(liquify::parse_reviewers("  ").empty());
}

} // namespace
