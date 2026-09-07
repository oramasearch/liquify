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

TEST(AppShellState, PersistsRepositoryPanelLayoutAndTheme) {
    const auto directory = TemporaryDirectory{};
    auto state = liquify::AppShellState{};
    state.sidebar_width = 312.0F;
    state.bottom_panel_height = 288.0F;
    state.editor_split_ratio = 0.42F;
    state.theme = liquify::ThemePreference::dark;
    state.high_contrast = true;
    state.sidebar_visible = false;
    state.bottom_panel_visible = true;
    state.editor_split = true;
    auto panels = liquify::PanelRegistry(state.panel_layout);
    panels.move(liquify::PanelId::review, liquify::PanelPlacement::floating);
    panels.update_floating_rect(liquify::PanelId::review, {180.0F, 140.0F, 620.0F, 410.0F});
    panels.move(liquify::PanelId::assistant, liquify::PanelPlacement::editor_tab);
    panels.focus(liquify::PanelId::assistant);
    state.panel_layout = panels.state();

    ASSERT_TRUE(liquify::save_app_shell_state(directory.path(), state).empty());
    const auto loaded = liquify::load_app_shell_state(directory.path());
    const auto loaded_panels = liquify::PanelRegistry(loaded.state.panel_layout);

    ASSERT_TRUE(loaded.error.empty());
    EXPECT_FLOAT_EQ(loaded.state.sidebar_width, 312.0F);
    EXPECT_FLOAT_EQ(loaded.state.bottom_panel_height, 288.0F);
    EXPECT_FLOAT_EQ(loaded.state.editor_split_ratio, 0.42F);
    EXPECT_EQ(loaded.state.theme, liquify::ThemePreference::dark);
    EXPECT_TRUE(loaded.state.high_contrast);
    EXPECT_FALSE(loaded.state.sidebar_visible);
    EXPECT_TRUE(loaded.state.bottom_panel_visible);
    EXPECT_TRUE(loaded.state.editor_split);
    EXPECT_EQ(loaded_panels.layout(liquify::PanelId::review).placement,
              liquify::PanelPlacement::floating);
    EXPECT_FLOAT_EQ(loaded_panels.layout(liquify::PanelId::review).floating_rect.width, 620.0F);
    EXPECT_EQ(loaded_panels.layout(liquify::PanelId::assistant).placement,
              liquify::PanelPlacement::editor_tab);
    EXPECT_EQ(loaded_panels.active(liquify::PanelPlacement::editor_tab),
              liquify::PanelId::assistant);
}

TEST(AppShellState, ReadsVersionOneSelection) {
    const auto directory = TemporaryDirectory{};
    std::filesystem::create_directories(directory.path() / ".liquid");
    auto output = std::ofstream(liquify::app_shell_state_path(directory.path()));
    output << R"({"version":1,"sidebar":"review","bottomPanel":"assistant"})";
    output.close();

    const auto loaded = liquify::load_app_shell_state(directory.path());
    const auto panels = liquify::PanelRegistry(loaded.state.panel_layout);

    ASSERT_TRUE(loaded.error.empty());
    EXPECT_EQ(panels.active(liquify::PanelPlacement::sidebar), liquify::PanelId::review);
    EXPECT_EQ(panels.active(liquify::PanelPlacement::bottom), liquify::PanelId::assistant);
}

TEST(AppShellState, ResetRestoresLayoutAndPreservesTheme) {
    auto state = liquify::AppShellState{};
    state.theme = liquify::ThemePreference::dark;
    state.high_contrast = true;
    state.sidebar_width = 400.0F;
    state.sidebar_visible = false;
    auto panels = liquify::PanelRegistry(state.panel_layout);
    panels.move(liquify::PanelId::diagnostics, liquify::PanelPlacement::floating);
    state.panel_layout = panels.state();

    liquify::reset_app_shell_layout(state);
    const auto reset_panels = liquify::PanelRegistry(state.panel_layout);

    EXPECT_EQ(state.theme, liquify::ThemePreference::dark);
    EXPECT_TRUE(state.high_contrast);
    EXPECT_FLOAT_EQ(state.sidebar_width, 260.0F);
    EXPECT_TRUE(state.sidebar_visible);
    EXPECT_EQ(reset_panels.layout(liquify::PanelId::diagnostics).placement,
              liquify::PanelPlacement::bottom);
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
