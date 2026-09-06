#include "liquify/review.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <mutex>
#include <span>
#include <string_view>
#include <utility>

#include "liquid/repository.hpp"
#include "liquid/sync.hpp"

namespace liquify {
namespace {

auto same_change(const liquid::change_id& left, const liquid::change_id& right) -> bool {
    return liquid::to_string(left.value) == liquid::to_string(right.value);
}

auto lines(std::span<const std::byte> contents) -> std::vector<std::string> {
    const auto text =
        std::string_view{reinterpret_cast<const char*>(contents.data()), contents.size()};
    auto output = std::vector<std::string>{};
    auto offset = std::size_t{};
    while (offset < text.size()) {
        const auto newline = text.find('\n', offset);
        const auto length =
            newline == std::string_view::npos ? text.size() - offset : newline - offset;
        output.emplace_back(text.substr(offset, length));
        if (newline == std::string_view::npos) {
            break;
        }
        offset = newline + 1;
    }
    return output;
}

auto load_lines(liquid::repository& repository, const std::optional<liquid::digest>& blob)
    -> liquid::runtime::task<std::vector<std::string>> {
    if (!blob) {
        co_return std::vector<std::string>{};
    }
    auto contents = co_await repository.blob(*blob);
    if (!contents) {
        co_return std::unexpected(contents.error());
    }
    co_return lines(*contents);
}

auto anchor_state(const liquid::forge_comment& comment) -> ReviewAnchorState {
    if (!comment.mapped) {
        return ReviewAnchorState::exact;
    }
    if (comment.mapped->deleted) {
        return ReviewAnchorState::deleted;
    }
    if (comment.mapped->approximate) {
        return ReviewAnchorState::approximate;
    }
    if (comment.mapped->moved) {
        return ReviewAnchorState::moved;
    }
    return ReviewAnchorState::exact;
}

auto comments(std::vector<liquid::forge_comment> input) -> std::vector<ReviewComment> {
    auto output = std::vector<ReviewComment>{};
    output.reserve(input.size());
    for (auto& comment : input) {
        const auto state = anchor_state(comment);
        output.push_back({std::move(comment), state});
    }
    return output;
}

auto checked_line(const std::vector<std::string>& source, std::uint64_t one_based)
    -> std::string_view {
    if (one_based == 0 || one_based > source.size()) {
        return {};
    }
    return source[static_cast<std::size_t>(one_based - 1)];
}

void append_hunks(std::vector<ReviewDiffLine>& output,
                  const liquid::tree_diff_entry& entry,
                  const std::vector<std::string>& old_lines,
                  const std::vector<std::string>& new_lines) {
    if (!entry.line_hunks) {
        output.push_back(
            {ReviewDiffLineKind::context, "  binary or non-text value changed", entry.path});
        return;
    }
    for (const auto& hunk : *entry.line_hunks) {
        output.push_back({ReviewDiffLineKind::hunk,
                          std::format("@@ -{},{} +{},{} @@",
                                      hunk.old_line,
                                      hunk.old_count,
                                      hunk.new_line,
                                      hunk.new_count),
                          entry.path});
        for (auto offset = std::uint64_t{}; offset < hunk.old_count; ++offset) {
            const auto line = hunk.old_line + offset;
            output.push_back({ReviewDiffLineKind::removed,
                              "- " + std::string{checked_line(old_lines, line)},
                              entry.path,
                              line,
                              std::nullopt});
        }
        for (auto offset = std::uint64_t{}; offset < hunk.new_count; ++offset) {
            const auto line = hunk.new_line + offset;
            output.push_back({ReviewDiffLineKind::added,
                              "+ " + std::string{checked_line(new_lines, line)},
                              entry.path,
                              std::nullopt,
                              line});
        }
    }
}

auto append_conflicts(liquid::repository& repository,
                      std::vector<ReviewDiffLine>& output,
                      const liquid::tree_diff_entry& entry) -> liquid::runtime::task<void> {
    auto values = entry.new_conflicts;
    values.insert(values.end(), entry.old_conflicts.begin(), entry.old_conflicts.end());
    for (const auto& value : values) {
        auto contents = co_await repository.blob(value);
        if (!contents) {
            co_return std::unexpected(contents.error());
        }
        const auto value_lines = lines(*contents);
        const auto label = liquid::to_string(value);
        if (value_lines.empty()) {
            output.push_back(
                {ReviewDiffLineKind::conflict, "! conflict " + label + ": <empty>", entry.path});
            continue;
        }
        for (const auto& line : value_lines) {
            output.push_back(
                {ReviewDiffLineKind::conflict, "! conflict " + label + ": " + line, entry.path});
        }
    }
    co_return liquid::result<void>{};
}

auto render_diff_impl(liquid::repository& repository,
                      liquid::change_id base,
                      liquid::change_id head)
    -> liquid::runtime::task<std::vector<ReviewDiffLine>> {
    auto changed = co_await repository.diff(base, head, {.text = true});
    if (!changed) {
        co_return std::unexpected(changed.error());
    }
    auto output = std::vector<ReviewDiffLine>{};
    for (const auto& entry : *changed) {
        if (entry.kind != liquid::node_kind::blob && entry.old_conflicts.empty() &&
            entry.new_conflicts.empty()) {
            continue;
        }
        output.push_back({ReviewDiffLineKind::file, "text diff -- " + entry.path, entry.path});
        auto old_lines = co_await load_lines(repository, entry.old_blob);
        if (!old_lines) {
            co_return std::unexpected(old_lines.error());
        }
        auto new_lines = co_await load_lines(repository, entry.new_blob);
        if (!new_lines) {
            co_return std::unexpected(new_lines.error());
        }
        append_hunks(output, entry, *old_lines, *new_lines);
        auto conflicts = co_await append_conflicts(repository, output, entry);
        if (!conflicts) {
            co_return std::unexpected(conflicts.error());
        }
    }
    if (output.empty()) {
        output.push_back({ReviewDiffLineKind::context, "No text changes."});
    }
    co_return output;
}

} // namespace

auto build_review_diff(liquid::repository& repository,
                       liquid::change_id base,
                       liquid::change_id head)
    -> liquid::runtime::task<std::vector<ReviewDiffLine>> {
    co_return co_await render_diff_impl(repository, base, head);
}

ReviewModel::ReviewModel(std::filesystem::path repository_path,
                         std::string lake_url,
                         std::string bearer_token)
    : repository_path_(std::move(repository_path)), lake_url_(std::move(lake_url)),
      bearer_token_(std::move(bearer_token)), forge_(lake_url_, {.bearer_token = bearer_token_}) {}

auto ReviewModel::list_reviews() -> liquid::runtime::task<ReviewSnapshot> {
    auto listed = co_await forge_.list_reviews();
    if (!listed) {
        co_return std::unexpected(listed.error());
    }
    current_.reviews = std::move(*listed);
    co_return current_;
}

auto ReviewModel::pull(liquid::repository& repository) -> liquid::runtime::task<void> {
    auto endpoint = liquid::sync_endpoint{
        lake_url_, liquid::sync_endpoint_options{.bearer_token = bearer_token_}};
    auto pulled = co_await endpoint.pull(repository);
    if (!pulled) {
        co_return std::unexpected(pulled.error());
    }
    co_return liquid::result<void>{};
}

auto ReviewModel::rebuild_diff(liquid::repository& repository) -> liquid::runtime::task<void> {
    if (!current_.base || !current_.head) {
        co_return std::unexpected(
            liquid::error{liquid::errc::invalid_argument, "review has no diff range", "review"});
    }
    auto rendered = co_await build_review_diff(repository, *current_.base, *current_.head);
    if (!rendered) {
        co_return std::unexpected(rendered.error());
    }
    current_.diff = std::move(*rendered);
    co_return liquid::result<void>{};
}

auto ReviewModel::reload_comments() -> liquid::runtime::task<void> {
    if (!current_.selected) {
        co_return std::unexpected(
            liquid::error{liquid::errc::invalid_argument, "no review is open", "review"});
    }
    auto listed = co_await forge_.list_comments(current_.selected->id);
    if (!listed) {
        co_return std::unexpected(listed.error());
    }
    current_.comments = comments(std::move(*listed));
    co_return liquid::result<void>{};
}

auto ReviewModel::open_review(std::string id) -> liquid::runtime::task<ReviewSnapshot> {
    const auto started = std::chrono::steady_clock::now();
    auto review = co_await forge_.get_review(std::move(id));
    if (!review) {
        co_return std::unexpected(review.error());
    }
    auto repository = co_await liquid::repository::open(repository_path_);
    if (!repository) {
        co_return std::unexpected(repository.error());
    }
    const auto status = repository->status();
    if (!status) {
        co_return std::unexpected(status.error());
    }
    const auto prior_head = status->head;
    auto pulled = co_await pull(*repository);
    if (!pulled) {
        co_return std::unexpected(pulled.error());
    }
    auto head = co_await repository->head();
    if (!head) {
        co_return std::unexpected(head.error());
    }
    auto history = co_await repository->log({.from = review->change, .first_parent = true});
    if (!history) {
        co_return std::unexpected(history.error());
    }
    auto base = prior_head;
    if (!history->empty() && !history->front().parents.empty()) {
        base = history->front().parents.front().id;
    }
    if (!base) {
        co_return std::unexpected(
            liquid::error{liquid::errc::not_found, "review change has no parent", review->id});
    }
    current_.selected = std::move(*review);
    current_.base = *base;
    current_.head = *head;
    if (!same_change(current_.selected->change, *head)) {
        auto mapped = co_await forge_.advance_anchors(*repository, current_.selected->id, *head);
        if (!mapped) {
            co_return std::unexpected(mapped.error());
        }
        current_.comments = comments(std::move(*mapped));
    } else {
        auto loaded = co_await reload_comments();
        if (!loaded) {
            co_return std::unexpected(loaded.error());
        }
    }
    auto rebuilt = co_await rebuild_diff(*repository);
    if (!rebuilt) {
        co_return std::unexpected(rebuilt.error());
    }
    current_.open_to_first_hunk_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    co_return current_;
}

auto ReviewModel::refresh() -> liquid::runtime::task<ReviewSnapshot> {
    if (!current_.selected) {
        co_return co_await list_reviews();
    }
    auto repository = co_await liquid::repository::open(repository_path_);
    if (!repository) {
        co_return std::unexpected(repository.error());
    }
    auto pulled = co_await pull(*repository);
    if (!pulled) {
        co_return std::unexpected(pulled.error());
    }
    auto head = co_await repository->head();
    if (!head) {
        co_return std::unexpected(head.error());
    }
    auto review = co_await forge_.get_review(current_.selected->id);
    if (!review) {
        co_return std::unexpected(review.error());
    }
    current_.selected = std::move(*review);
    if (!current_.head || !same_change(*current_.head, *head)) {
        const auto started = std::chrono::steady_clock::now();
        auto mapped = co_await forge_.advance_anchors(*repository, current_.selected->id, *head);
        if (!mapped) {
            co_return std::unexpected(mapped.error());
        }
        current_.comments = comments(std::move(*mapped));
        current_.head = *head;
        auto rebuilt = co_await rebuild_diff(*repository);
        if (!rebuilt) {
            co_return std::unexpected(rebuilt.error());
        }
        current_.anchor_mapping_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
    } else {
        auto loaded = co_await reload_comments();
        if (!loaded) {
            co_return std::unexpected(loaded.error());
        }
    }
    co_return current_;
}

auto ReviewModel::create_comment(std::string body,
                                 std::string path,
                                 std::uint64_t line,
                                 std::uint64_t column) -> liquid::runtime::task<ReviewSnapshot> {
    if (!current_.selected || !current_.head) {
        co_return std::unexpected(
            liquid::error{liquid::errc::invalid_argument, "no review is open", "review comment"});
    }
    auto created = co_await forge_.create_comment(
        current_.selected->id,
        std::move(body),
        {.change = *current_.head, .path = std::move(path), .line = line, .column = column});
    if (!created) {
        co_return std::unexpected(created.error());
    }
    current_.comments.push_back({std::move(*created), ReviewAnchorState::exact});
    co_return current_;
}

auto ReviewModel::set_state(liquid::forge_review_state state)
    -> liquid::runtime::task<ReviewSnapshot> {
    if (!current_.selected) {
        co_return std::unexpected(
            liquid::error{liquid::errc::invalid_argument, "no review is open", "review state"});
    }
    auto updated = co_await forge_.set_review_state(current_.selected->id, state);
    if (!updated) {
        co_return std::unexpected(updated.error());
    }
    current_.selected = std::move(*updated);
    const auto found =
        std::ranges::find(current_.reviews, current_.selected->id, &liquid::forge_review::id);
    if (found != current_.reviews.end()) {
        *found = *current_.selected;
    }
    co_return current_;
}

auto ReviewModel::approve() -> liquid::runtime::task<ReviewSnapshot> {
    co_return co_await set_state(liquid::forge_review_state::approved);
}

auto ReviewModel::request_changes() -> liquid::runtime::task<ReviewSnapshot> {
    co_return co_await set_state(liquid::forge_review_state::open);
}

auto ReviewModel::snapshot() const -> const ReviewSnapshot& {
    return current_;
}

struct ReviewController::State {
    State(std::filesystem::path repository_path, std::string lake_url, std::string bearer_token)
        : model(std::move(repository_path), std::move(lake_url), std::move(bearer_token)) {}

    mutable std::mutex mutex;
    ReviewModel model;
    ReviewControllerSnapshot current;
    std::uint64_t pending{};
};

namespace {

using ReviewOperation = std::function<liquid::runtime::task<ReviewSnapshot>(ReviewModel&)>;

auto run_review_operation(std::shared_ptr<ReviewController::State> state, ReviewOperation operation)
    -> liquid::runtime::task<void> {
    auto result = co_await operation(state->model);
    auto lock = std::lock_guard(state->mutex);
    --state->pending;
    state->current.busy = state->pending != 0;
    ++state->current.generation;
    if (!result) {
        state->current.error = result.error().context.empty()
                                   ? result.error().message
                                   : result.error().context + ": " + result.error().message;
        co_return liquid::result<void>{};
    }
    state->current.review = std::move(*result);
    state->current.error.clear();
    co_return liquid::result<void>{};
}

void enqueue_review(const std::shared_ptr<ReviewController::State>& state,
                    ReviewOperation operation) {
    {
        auto lock = std::lock_guard(state->mutex);
        ++state->pending;
        state->current.busy = true;
    }
    liquid::runtime::spawn_serial(run_review_operation(state, std::move(operation)),
                                  [state](liquid::result<void> completed) {
                                      if (completed) {
                                          return;
                                      }
                                      auto lock = std::lock_guard(state->mutex);
                                      --state->pending;
                                      state->current.busy = state->pending != 0;
                                      state->current.error = completed.error().message;
                                      ++state->current.generation;
                                  });
}

} // namespace

ReviewController::ReviewController(std::filesystem::path repository_path,
                                   std::string lake_url,
                                   std::string bearer_token)
    : state_(std::make_shared<State>(
          std::move(repository_path), std::move(lake_url), std::move(bearer_token))) {}

ReviewController::~ReviewController() = default;

void ReviewController::request_list() {
    enqueue_review(state_, [](ReviewModel& model) { return model.list_reviews(); });
}

void ReviewController::request_open(std::string id) {
    enqueue_review(state_, [id = std::move(id)](ReviewModel& model) mutable {
        return model.open_review(std::move(id));
    });
}

void ReviewController::request_refresh() {
    enqueue_review(state_, [](ReviewModel& model) { return model.refresh(); });
}

void ReviewController::request_comment(std::string body,
                                       std::string path,
                                       std::uint64_t line,
                                       std::uint64_t column) {
    enqueue_review(
        state_,
        [body = std::move(body), path = std::move(path), line, column](ReviewModel& model) mutable {
            return model.create_comment(std::move(body), std::move(path), line, column);
        });
}

void ReviewController::request_approve() {
    enqueue_review(state_, [](ReviewModel& model) { return model.approve(); });
}

void ReviewController::request_changes() {
    enqueue_review(state_, [](ReviewModel& model) { return model.request_changes(); });
}

auto ReviewController::snapshot() const -> ReviewControllerSnapshot {
    auto lock = std::lock_guard(state_->mutex);
    return state_->current;
}

auto review_state_name(liquid::forge_review_state state) -> std::string_view {
    switch (state) {
    case liquid::forge_review_state::open:
        return "open";
    case liquid::forge_review_state::approved:
        return "approved";
    case liquid::forge_review_state::closed:
        return "closed";
    }
    return "unknown";
}

auto review_anchor_state_name(ReviewAnchorState state) -> std::string_view {
    switch (state) {
    case ReviewAnchorState::exact:
        return "exact";
    case ReviewAnchorState::moved:
        return "moved";
    case ReviewAnchorState::approximate:
        return "approximate";
    case ReviewAnchorState::deleted:
        return "deleted";
    }
    return "unknown";
}

} // namespace liquify
