#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "liquify/app_state.hpp"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = base / ("liquify-app-state-" + std::to_string(suffix));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const auto& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

TEST(AppShellState, PersistsRepositoryLayoutAndTheme) {
    const auto directory = TemporaryDirectory{};
    auto state = liquify::AppShellState{};
    state.sidebar_width = 312.0F;
    state.bottom_panel_height = 288.0F;
    state.editor_split_ratio = 0.42F;
    state.theme = liquify::ThemePreference::dark;
    state.sidebar = liquify::SidebarSection::review;
    state.bottom_panel = liquify::BottomPanelTab::assistant;
    state.sidebar_visible = false;
    state.bottom_panel_visible = true;
    state.editor_split = true;

    ASSERT_TRUE(liquify::save_app_shell_state(directory.path(), state).empty());
    const auto loaded = liquify::load_app_shell_state(directory.path());

    ASSERT_TRUE(loaded.error.empty());
    EXPECT_FLOAT_EQ(loaded.state.sidebar_width, 312.0F);
    EXPECT_FLOAT_EQ(loaded.state.bottom_panel_height, 288.0F);
    EXPECT_FLOAT_EQ(loaded.state.editor_split_ratio, 0.42F);
    EXPECT_EQ(loaded.state.theme, liquify::ThemePreference::dark);
    EXPECT_EQ(loaded.state.sidebar, liquify::SidebarSection::review);
    EXPECT_EQ(loaded.state.bottom_panel, liquify::BottomPanelTab::assistant);
    EXPECT_FALSE(loaded.state.sidebar_visible);
    EXPECT_TRUE(loaded.state.bottom_panel_visible);
    EXPECT_TRUE(loaded.state.editor_split);
}

TEST(AppShellState, UsesDefaultsForMissingOrInvalidState) {
    const auto missing = TemporaryDirectory{};
    const auto defaults = liquify::load_app_shell_state(missing.path());
    EXPECT_TRUE(defaults.error.empty());
    EXPECT_FLOAT_EQ(defaults.state.sidebar_width, 260.0F);
    EXPECT_EQ(defaults.state.theme, liquify::ThemePreference::system);

    const auto invalid = TemporaryDirectory{};
    std::filesystem::create_directories(invalid.path() / ".liquid");
    auto output = std::ofstream(liquify::app_shell_state_path(invalid.path()));
    output << "not json";
    output.close();
    const auto loaded = liquify::load_app_shell_state(invalid.path());
    EXPECT_FALSE(loaded.error.empty());
    EXPECT_FLOAT_EQ(loaded.state.sidebar_width, 260.0F);
}

} // namespace
