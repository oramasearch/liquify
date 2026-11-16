#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquify/user_error.hpp"

namespace liquify {

enum class RepositoryLifecyclePhase : std::uint8_t {
    welcome,
    opening,
    cloning,
    open,
    unreadable,
    offline,
    failed,
};

struct RepositorySummary {
    std::filesystem::path path;
    std::string name;
    std::string last_change;
    std::string last_changed_at;
    std::string branch;
    std::string current_change;
    std::string offline_reason;
    std::uint64_t remote_bytes{};
    std::uint64_t hydration_debt{};
    std::uint64_t pending_uploads{};
    std::size_t presence_count{};
};

struct RecentRepository {
    std::filesystem::path path;
    std::string name;
    std::string last_change;
    std::string last_changed_at;
    std::string branch;
    std::chrono::sys_seconds opened_at{};
};

struct IdentitySession {
    std::string actor;
    std::string custody;
};

class FirstRunState {
public:
    void begin_open(std::filesystem::path path);
    void begin_clone(std::string remote, std::filesystem::path path);
    void
    repository_opened(RepositorySummary summary,
                      std::chrono::sys_seconds opened_at = std::chrono::floor<std::chrono::seconds>(
                          std::chrono::system_clock::now()));
    void open_failed(std::filesystem::path path, std::string error);
    void clone_failed(UserError failure);
    void return_to_welcome();
    void sign_in(IdentitySession session);
    void sign_out();
    void restore(std::vector<RecentRepository> recent, std::optional<IdentitySession> session = {});

    [[nodiscard]] auto phase() const noexcept -> RepositoryLifecyclePhase;
    [[nodiscard]] auto active_repository() const -> const std::optional<RepositorySummary>&;
    [[nodiscard]] auto pending_path() const -> const std::filesystem::path&;
    [[nodiscard]] auto clone_remote() const -> const std::string&;
    [[nodiscard]] auto error() const -> const std::string&;
    // Why the last clone failed; set in the offline and failed phases.
    [[nodiscard]] auto clone_failure() const -> const UserError&;
    [[nodiscard]] auto recent() const -> std::span<const RecentRepository>;
    [[nodiscard]] auto session() const -> const std::optional<IdentitySession>&;

private:
    void remember(const RepositorySummary& summary, std::chrono::sys_seconds opened_at);

    RepositoryLifecyclePhase phase_{RepositoryLifecyclePhase::welcome};
    std::optional<RepositorySummary> active_repository_;
    std::filesystem::path pending_path_;
    std::string clone_remote_;
    std::string error_;
    UserError clone_failure_;
    std::vector<RecentRepository> recent_;
    std::optional<IdentitySession> session_;
};

struct FirstRunStateLoad {
    FirstRunState state;
    std::string error;
};

[[nodiscard]] auto default_first_run_state_path() -> std::filesystem::path;
[[nodiscard]] auto load_first_run_state(const std::filesystem::path& path) -> FirstRunStateLoad;
[[nodiscard]] auto save_first_run_state(const std::filesystem::path& path,
                                        const FirstRunState& state) -> std::string;

struct StatusBarInput {
    bool repository_open{};
    bool daemon_running{};
    bool daemon_available{};
    bool sync_running{};
    bool hydration_running{};
    bool build_running{};
    bool offline{};
    std::uint64_t pending_uploads{};
    std::uint64_t hydration_debt{};
    std::uint64_t remote_bytes{};
    std::size_t presence_count{};
    std::size_t notification_count{};
    std::string build_result;
    std::string current_change;
    std::string branch;
    std::string session;
};

[[nodiscard]] auto status_bar_segments(const StatusBarInput& input) -> std::vector<std::string>;
[[nodiscard]] auto short_status_value(std::string_view value) -> std::string;

} // namespace liquify
