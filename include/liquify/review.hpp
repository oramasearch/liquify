#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/forge.hpp"
#include "liquid/runtime.hpp"

namespace liquify {

enum class ReviewDiffLineKind : std::uint8_t {
    file,
    hunk,
    context,
    added,
    removed,
    conflict,
};

struct ReviewDiffLine {
    ReviewDiffLineKind kind{ReviewDiffLineKind::context};
    std::string text;
    std::string path;
    std::optional<std::uint64_t> old_line;
    std::optional<std::uint64_t> new_line;
};

enum class ReviewAnchorState : std::uint8_t {
    exact,
    moved,
    approximate,
    deleted,
};

struct ReviewComment {
    liquid::forge_comment value;
    ReviewAnchorState state{ReviewAnchorState::exact};
};

struct ReviewSnapshot {
    std::vector<liquid::forge_review> reviews;
    std::optional<liquid::forge_review> selected;
    std::vector<ReviewDiffLine> diff;
    std::vector<ReviewComment> comments;
    std::optional<liquid::change_id> base;
    std::optional<liquid::change_id> head;
    double open_to_first_hunk_ms{};
    double anchor_mapping_ms{};
};

[[nodiscard]] auto
build_review_diff(liquid::repository& repository, liquid::change_id base, liquid::change_id head)
    -> liquid::runtime::task<std::vector<ReviewDiffLine>>;

class ReviewModel {
public:
    ReviewModel(std::filesystem::path repository_path,
                std::string lake_url,
                std::string bearer_token = {});

    [[nodiscard]] auto list_reviews() -> liquid::runtime::task<ReviewSnapshot>;
    [[nodiscard]] auto open_review(std::string id) -> liquid::runtime::task<ReviewSnapshot>;
    [[nodiscard]] auto refresh() -> liquid::runtime::task<ReviewSnapshot>;
    [[nodiscard]] auto
    create_comment(std::string body, std::string path, std::uint64_t line, std::uint64_t column)
        -> liquid::runtime::task<ReviewSnapshot>;
    [[nodiscard]] auto approve() -> liquid::runtime::task<ReviewSnapshot>;
    // Asks for a review of the local repository's current change (its head) on lake, through
    // forge_client::create_review, and opens it. The change must already be pushed.
    [[nodiscard]] auto request_review(std::vector<std::string> reviewers)
        -> liquid::runtime::task<ReviewSnapshot>;
    [[nodiscard]] auto request_changes() -> liquid::runtime::task<ReviewSnapshot>;

    [[nodiscard]] auto snapshot() const -> const ReviewSnapshot&;

private:
    [[nodiscard]] auto pull(liquid::repository& repository) -> liquid::runtime::task<void>;
    [[nodiscard]] auto rebuild_diff(liquid::repository& repository) -> liquid::runtime::task<void>;
    [[nodiscard]] auto reload_comments() -> liquid::runtime::task<void>;
    [[nodiscard]] auto set_state(liquid::forge_review_state state)
        -> liquid::runtime::task<ReviewSnapshot>;

    std::filesystem::path repository_path_;
    std::string lake_url_;
    std::string bearer_token_;
    liquid::forge_client forge_;
    ReviewSnapshot current_;
};

struct ReviewControllerSnapshot {
    ReviewSnapshot review;
    std::string error;
    std::uint64_t generation{};
    bool busy{};
};

class ReviewController {
public:
    struct State;

    ReviewController(std::filesystem::path repository_path,
                     std::string lake_url,
                     std::string bearer_token = {});
    ~ReviewController();
    ReviewController(const ReviewController&) = delete;
    auto operator=(const ReviewController&) -> ReviewController& = delete;
    ReviewController(ReviewController&&) = delete;
    auto operator=(ReviewController&&) -> ReviewController& = delete;

    void request_list();
    void request_open(std::string id);
    void request_refresh();
    void
    request_comment(std::string body, std::string path, std::uint64_t line, std::uint64_t column);
    void request_approve();
    void request_review(std::vector<std::string> reviewers);
    void request_changes();

    [[nodiscard]] auto snapshot() const -> ReviewControllerSnapshot;

private:
    std::shared_ptr<State> state_;
};

// "ana, bo ,, cy" -> {"ana", "bo", "cy"}.
[[nodiscard]] auto parse_reviewers(std::string_view text) -> std::vector<std::string>;
[[nodiscard]] auto review_state_name(liquid::forge_review_state state) -> std::string_view;
[[nodiscard]] auto review_anchor_state_name(ReviewAnchorState state) -> std::string_view;

} // namespace liquify
