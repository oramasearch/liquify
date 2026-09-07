#pragma once

#include <filesystem>
#include <string>

#include "liquify/panel.hpp"

namespace liquify {

enum class ThemePreference {
    system,
    light,
    dark,
};

struct AppShellState {
    float sidebar_width = 260.0F;
    float bottom_panel_height = 260.0F;
    float editor_split_ratio = 0.58F;
    ThemePreference theme = ThemePreference::system;
    PanelRegistryState panel_layout = default_panel_registry_state();
    bool high_contrast = false;
    bool sidebar_visible = true;
    bool bottom_panel_visible = true;
    bool editor_split = false;
};

struct AppShellStateLoad {
    AppShellState state;
    std::string error;
};

[[nodiscard]] auto app_shell_state_path(const std::filesystem::path& repository)
    -> std::filesystem::path;
[[nodiscard]] auto load_app_shell_state(const std::filesystem::path& repository)
    -> AppShellStateLoad;
[[nodiscard]] auto save_app_shell_state(const std::filesystem::path& repository,
                                        const AppShellState& state) -> std::string;
void reset_app_shell_layout(AppShellState& state);

} // namespace liquify
