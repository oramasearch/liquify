// SPDX-License-Identifier: MIT

#include "liquify/first_run.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <string_view>
#include <utility>

#include <boost/json.hpp>

namespace liquify {
namespace {

constexpr auto state_version = 1;
constexpr auto recent_limit = std::size_t{8};

auto string_member(const boost::json::object& object, std::string_view name)
    -> std::optional<std::string> {
    const auto* value = object.if_contains(name);
    if (value == nullptr || !value->is_string()) {
        return std::nullopt;
    }
    return std::string(value->as_string());
}

auto recent_json(const RecentRepository& recent) -> boost::json::object {
    return {
        {"path", recent.path.string()},
        {"name", recent.name},
        {"lastChange", recent.last_change},
        {"lastChangedAt", recent.last_changed_at},
        {"branch", recent.branch},
        {"openedAt", recent.opened_at.time_since_epoch().count()},
    };
}

auto parse_recent(const boost::json::value& value) -> std::optional<RecentRepository> {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto& object = value.as_object();
    const auto path = string_member(object, "path");
    const auto name = string_member(object, "name");
    const auto* opened = object.if_contains("openedAt");
    if (!path || path->empty() || !name || opened == nullptr ||
        (!opened->is_int64() && !opened->is_uint64())) {
        return std::nullopt;
    }
    const auto seconds = opened->to_number<std::int64_t>();
    return RecentRepository{
        .path = *path,
        .name = *name,
        .last_change = string_member(object, "lastChange").value_or("No named change"),
        .last_changed_at = string_member(object, "lastChangedAt").value_or(""),
        .branch = string_member(object, "branch").value_or("No branch"),
        .opened_at = std::chrono::sys_seconds{std::chrono::seconds{seconds}},
    };
}

auto byte_count(std::uint64_t bytes) -> std::string {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return std::to_string(bytes / (1024ULL * 1024ULL * 1024ULL)) + " GiB";
    }
    if (bytes >= 1024ULL * 1024ULL) {
        return std::to_string(bytes / (1024ULL * 1024ULL)) + " MiB";
    }
    if (bytes >= 1024ULL) {
        return std::to_string(bytes / 1024ULL) + " KiB";
    }
    return std::to_string(bytes) + " B";
}

} // namespace

void FirstRunState::begin_open(std::filesystem::path path) {
    phase_ = RepositoryLifecyclePhase::opening;
    pending_path_ = std::move(path);
    clone_remote_.clear();
    error_.clear();
    clone_failure_ = {};
}

void FirstRunState::begin_clone(std::string remote, std::filesystem::path path) {
    phase_ = RepositoryLifecyclePhase::cloning;
    clone_remote_ = std::move(remote);
    pending_path_ = std::move(path);
    error_.clear();
    clone_failure_ = {};
}

void FirstRunState::repository_opened(RepositorySummary summary,
                                      std::chrono::sys_seconds opened_at) {
    remember(summary, opened_at);
    active_repository_ = std::move(summary);
    pending_path_.clear();
    clone_remote_.clear();
    error_.clear();
    clone_failure_ = {};
    phase_ = RepositoryLifecyclePhase::open;
}

void FirstRunState::open_failed(std::filesystem::path path, std::string error) {
    active_repository_.reset();
    pending_path_ = std::move(path);
    clone_remote_.clear();
    error_ = std::move(error);
    phase_ = RepositoryLifecyclePhase::unreadable;
}

void FirstRunState::clone_failed(UserError failure) {
    active_repository_.reset();
    error_ = failure.summary;
    phase_ = failure.offline() ? RepositoryLifecyclePhase::offline
                               : RepositoryLifecyclePhase::failed;
    clone_failure_ = std::move(failure);
}

auto FirstRunState::clone_failure() const -> const UserError& {
    return clone_failure_;
}

void FirstRunState::return_to_welcome() {
    active_repository_.reset();
    pending_path_.clear();
    clone_remote_.clear();
    error_.clear();
    clone_failure_ = {};
    phase_ = RepositoryLifecyclePhase::welcome;
}

void FirstRunState::sign_in(IdentitySession session) {
    session_ = std::move(session);
}

void FirstRunState::sign_out() {
    session_.reset();
}

void FirstRunState::restore(std::vector<RecentRepository> recent,
                            std::optional<IdentitySession> session) {
    recent_ = std::move(recent);
    std::ranges::sort(recent_, std::greater{}, &RecentRepository::opened_at);
    if (recent_.size() > recent_limit) {
        recent_.resize(recent_limit);
    }
    session_ = std::move(session);
}

auto FirstRunState::phase() const noexcept -> RepositoryLifecyclePhase {
    return phase_;
}

auto FirstRunState::active_repository() const -> const std::optional<RepositorySummary>& {
    return active_repository_;
}

auto FirstRunState::pending_path() const -> const std::filesystem::path& {
    return pending_path_;
}

auto FirstRunState::clone_remote() const -> const std::string& {
    return clone_remote_;
}

auto FirstRunState::error() const -> const std::string& {
    return error_;
}

auto FirstRunState::recent() const -> std::span<const RecentRepository> {
    return recent_;
}

auto FirstRunState::session() const -> const std::optional<IdentitySession>& {
    return session_;
}

void FirstRunState::remember(const RepositorySummary& summary, std::chrono::sys_seconds opened_at) {
    const auto same_path = [&summary](const RecentRepository& recent) {
        return recent.path == summary.path;
    };
    std::erase_if(recent_, same_path);
    recent_.insert(recent_.begin(),
                   RecentRepository{
                       .path = summary.path,
                       .name = summary.name,
                       .last_change = summary.last_change,
                       .last_changed_at = summary.last_changed_at,
                       .branch = summary.branch,
                       .opened_at = opened_at,
                   });
    if (recent_.size() > recent_limit) {
        recent_.resize(recent_limit);
    }
}

auto default_first_run_state_path() -> std::filesystem::path {
    if (const auto* root = std::getenv("LIQUID_HOME")) {
        return std::filesystem::path(root) / "liquify" / "first-run.json";
    }
#if defined(_WIN32)
    if (const auto* root = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(root) / "liquify" / "first-run.json";
    }
#endif
    if (const auto* root = std::getenv("HOME")) {
        return std::filesystem::path(root) / ".liquid" / "liquify" / "first-run.json";
    }
    return std::filesystem::path(".liquid") / "liquify" / "first-run.json";
}

auto load_first_run_state(const std::filesystem::path& path) -> FirstRunStateLoad {
    auto loaded = FirstRunStateLoad{};
    auto input = std::ifstream(path, std::ios::binary);
    if (!input) {
        return loaded;
    }
    const auto bytes = std::string(std::istreambuf_iterator<char>(input), {});
    auto parse_error = boost::system::error_code{};
    const auto value = boost::json::parse(bytes, parse_error);
    if (parse_error || !value.is_object()) {
        loaded.error = "cannot read " + path.string() + ": invalid JSON";
        return loaded;
    }
    const auto& object = value.as_object();
    auto recent = std::vector<RecentRepository>{};
    if (const auto* items = object.if_contains("recent"); items != nullptr && items->is_array()) {
        for (const auto& item : items->as_array()) {
            if (auto parsed = parse_recent(item)) {
                recent.push_back(std::move(*parsed));
            }
        }
    }
    auto session = std::optional<IdentitySession>{};
    if (const auto* session_value = object.if_contains("session");
        session_value != nullptr && session_value->is_object()) {
        const auto actor = string_member(session_value->as_object(), "actor");
        if (actor && !actor->empty()) {
            session = IdentitySession{
                .actor = *actor,
                .custody = string_member(session_value->as_object(), "custody").value_or("TAAP"),
            };
        }
    }
    loaded.state.restore(std::move(recent), std::move(session));
    return loaded;
}

auto save_first_run_state(const std::filesystem::path& path, const FirstRunState& state)
    -> std::string {
    auto filesystem_error = std::error_code{};
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), filesystem_error);
    }
    if (filesystem_error) {
        return "cannot create " + path.parent_path().string() + ": " + filesystem_error.message();
    }
    auto recent = boost::json::array{};
    for (const auto& item : state.recent()) {
        recent.push_back(recent_json(item));
    }
    auto object = boost::json::object{
        {"version", state_version},
        {"recent", std::move(recent)},
    };
    if (state.session()) {
        object["session"] = boost::json::object{
            {"actor", state.session()->actor},
            {"custody", state.session()->custody},
        };
    }
    auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return "cannot write " + path.string();
    }
    output << boost::json::serialize(object) << '\n';
    if (!output) {
        return "cannot write " + path.string();
    }
    return {};
}

auto short_status_value(std::string_view value) -> std::string {
    constexpr auto visible_characters = std::size_t{12};
    if (value.size() <= visible_characters) {
        return std::string(value);
    }
    return std::string(value.substr(0, visible_characters));
}

auto status_bar_segments(const StatusBarInput& input) -> std::vector<std::string> {
    auto output = std::vector<std::string>{};
    if (!input.repository_open) {
        output.push_back("No repository");
    }
    if (input.daemon_running) {
        output.push_back("Daemon on");
    } else if (input.daemon_available) {
        output.push_back("Daemon stopped");
    } else {
        output.push_back("Daemon unavailable");
    }
    if (input.sync_running) {
        output.push_back("Syncing");
    } else if (input.offline) {
        output.push_back("Offline");
    } else if (input.pending_uploads > 0) {
        output.push_back("Sync " + std::to_string(input.pending_uploads) + " pending");
    } else {
        output.push_back("Sync current");
    }
    const auto remote_bytes = std::max(input.hydration_debt, input.remote_bytes);
    if (input.hydration_running) {
        output.push_back("Hydrating");
    } else if (remote_bytes > 0) {
        output.push_back(byte_count(remote_bytes) + " remote");
    } else {
        output.push_back("Hydrated");
    }
    if (input.build_running) {
        output.push_back("Building");
    } else if (!input.build_result.empty()) {
        output.push_back("Build " + input.build_result);
    } else {
        output.push_back("Build idle");
    }
    output.push_back(std::to_string(input.presence_count) + " present");
    output.push_back(input.current_change.empty()
                         ? "No change"
                         : "Change " + short_status_value(input.current_change));
    output.push_back(input.branch.empty() ? "No branch" : input.branch);
    output.push_back(std::to_string(input.notification_count) + " notifications");
    output.push_back(input.session.empty() ? "Sign in" : short_status_value(input.session));
    return output;
}

} // namespace liquify
