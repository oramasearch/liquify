#include "liquify/first_run.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("liquify-first-run-" + std::to_string(++sequence_))) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::filesystem::remove_all(path_);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path& {
        return path_;
    }

private:
    inline static std::size_t sequence_{};
    std::filesystem::path path_;
};

auto summary(std::string name) -> liquify::RepositorySummary {
    return {
        .path = "/repositories/" + name,
        .name = std::move(name),
        .last_change = "Finish repository lifecycle",
        .last_changed_at = "2026-09-06",
        .branch = "issue-36-first-run",
        .current_change = "blake3:1234567890abcdef",
        .presence_count = 3,
    };
}

TEST(FirstRunState, OpensSwitchesAndRemembersRepositories) {
    auto state = liquify::FirstRunState{};
    const auto first_opened = std::chrono::sys_seconds{std::chrono::seconds{10}};
    const auto second_opened = std::chrono::sys_seconds{std::chrono::seconds{20}};

    state.begin_open("/repositories/first");
    EXPECT_EQ(state.phase(), liquify::RepositoryLifecyclePhase::opening);
    state.repository_opened(summary("first"), first_opened);
    EXPECT_EQ(state.phase(), liquify::RepositoryLifecyclePhase::open);
    ASSERT_TRUE(state.active_repository());
    EXPECT_EQ(state.active_repository()->name, "first");

    state.begin_open("/repositories/second");
    state.repository_opened(summary("second"), second_opened);
    ASSERT_EQ(state.recent().size(), 2U);
    EXPECT_EQ(state.recent()[0].name, "second");
    EXPECT_EQ(state.recent()[1].name, "first");

    state.begin_open("/repositories/first");
    state.repository_opened(summary("first"), second_opened + std::chrono::seconds{1});
    ASSERT_EQ(state.recent().size(), 2U);
    EXPECT_EQ(state.recent()[0].name, "first");
}

TEST(FirstRunState, DistinguishesUnreadableRepositoriesAndOfflineClones) {
    auto state = liquify::FirstRunState{};

    state.begin_open("/repositories/missing");
    state.open_failed("/repositories/missing", "permission denied");
    EXPECT_EQ(state.phase(), liquify::RepositoryLifecyclePhase::unreadable);
    EXPECT_EQ(state.pending_path(), "/repositories/missing");
    EXPECT_EQ(state.error(), "permission denied");

    state.begin_clone("https://lake.example/repository", "/repositories/new");
    state.clone_failed("lake is unreachable", true);
    EXPECT_EQ(state.phase(), liquify::RepositoryLifecyclePhase::offline);
    EXPECT_EQ(state.clone_remote(), "https://lake.example/repository");
    EXPECT_EQ(state.error(), "lake is unreachable");
}

TEST(FirstRunState, PersistsRecentRepositoriesAndIdentityWithoutSecrets) {
    const auto temporary = TemporaryDirectory{};
    const auto path = temporary.path() / "first-run.json";
    auto state = liquify::FirstRunState{};
    state.repository_opened(summary("liquify"), std::chrono::sys_seconds{std::chrono::seconds{42}});
    state.sign_in({.actor = "taap:1:ed25519:ABCDEF", .custody = "local device"});

    EXPECT_TRUE(liquify::save_first_run_state(path, state).empty());
    const auto loaded = liquify::load_first_run_state(path);
    EXPECT_TRUE(loaded.error.empty());
    ASSERT_EQ(loaded.state.recent().size(), 1U);
    EXPECT_EQ(loaded.state.recent()[0].name, "liquify");
    ASSERT_TRUE(loaded.state.session());
    EXPECT_EQ(loaded.state.session()->actor, "taap:1:ed25519:ABCDEF");
    EXPECT_EQ(loaded.state.session()->custody, "local device");

    auto input = std::ifstream(path);
    const auto contents = std::string(std::istreambuf_iterator<char>(input), {});
    EXPECT_EQ(contents.find("token"), std::string::npos);
    EXPECT_EQ(contents.find("secret"), std::string::npos);
}

TEST(FirstRunState, InvalidPersistedStateFallsBackToWelcome) {
    const auto temporary = TemporaryDirectory{};
    const auto path = temporary.path() / "first-run.json";
    auto output = std::ofstream(path);
    output << "not JSON";
    output.close();

    const auto loaded = liquify::load_first_run_state(path);
    EXPECT_FALSE(loaded.error.empty());
    EXPECT_EQ(loaded.state.phase(), liquify::RepositoryLifecyclePhase::welcome);
    EXPECT_TRUE(loaded.state.recent().empty());
}

TEST(StatusBar, ReportsEveryRepositoryAndSessionSignal) {
    const auto segments = liquify::status_bar_segments({
        .repository_open = true,
        .daemon_running = true,
        .daemon_available = true,
        .sync_running = false,
        .hydration_running = false,
        .build_running = false,
        .offline = false,
        .pending_uploads = 2,
        .hydration_debt = 4096,
        .remote_bytes = 0,
        .presence_count = 3,
        .notification_count = 4,
        .build_result = "passed",
        .current_change = "blake3:1234567890abcdef",
        .branch = "main",
        .session = "taap:1:ed25519:ABCDEF",
    });

    EXPECT_EQ(segments,
              (std::vector<std::string>{
                  "Daemon on",
                  "Sync 2 pending",
                  "4 KiB remote",
                  "Build passed",
                  "3 present",
                  "Change blake3:12345",
                  "main",
                  "4 notifications",
                  "taap:1:ed255",
              }));
}

TEST(StatusBar, ReportsWelcomeAndOfflineDefaults) {
    const auto segments = liquify::status_bar_segments({.offline = true});

    ASSERT_EQ(segments.size(), 10U);
    EXPECT_EQ(segments[0], "No repository");
    EXPECT_EQ(segments[1], "Daemon unavailable");
    EXPECT_EQ(segments[2], "Offline");
    EXPECT_EQ(segments[3], "Hydrated");
    EXPECT_EQ(segments[4], "Build idle");
    EXPECT_EQ(segments[9], "Sign in");
}

} // namespace
