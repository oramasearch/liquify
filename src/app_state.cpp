#include "liquify/app_state.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>

#include <boost/json.hpp>

namespace liquify {
namespace {

constexpr auto state_version = 1;

auto theme_name(ThemePreference theme) -> std::string_view {
    switch (theme) {
    case ThemePreference::system:
        return "system";
    case ThemePreference::light:
        return "light";
    case ThemePreference::dark:
        return "dark";
    }
    return "system";
}

auto sidebar_name(SidebarSection sidebar) -> std::string_view {
    switch (sidebar) {
    case SidebarSection::repository:
        return "repository";
    case SidebarSection::search:
        return "search";
    case SidebarSection::review:
        return "review";
    case SidebarSection::build:
        return "build";
    case SidebarSection::multiplayer:
        return "multiplayer";
    }
    return "repository";
}

auto bottom_panel_name(BottomPanelTab tab) -> std::string_view {
    switch (tab) {
    case BottomPanelTab::diagnostics:
        return "diagnostics";
    case BottomPanelTab::build:
        return "build";
    case BottomPanelTab::assistant:
        return "assistant";
    case BottomPanelTab::plugins:
        return "plugins";
    }
    return "diagnostics";
}

auto theme_from_name(std::string_view name) -> ThemePreference {
    if (name == "light") {
        return ThemePreference::light;
    }
    if (name == "dark") {
        return ThemePreference::dark;
    }
    return ThemePreference::system;
}

auto sidebar_from_name(std::string_view name) -> SidebarSection {
    if (name == "search") {
        return SidebarSection::search;
    }
    if (name == "review") {
        return SidebarSection::review;
    }
    if (name == "build") {
        return SidebarSection::build;
    }
    if (name == "multiplayer") {
        return SidebarSection::multiplayer;
    }
    return SidebarSection::repository;
}

auto bottom_panel_from_name(std::string_view name) -> BottomPanelTab {
    if (name == "build") {
        return BottomPanelTab::build;
    }
    if (name == "assistant") {
        return BottomPanelTab::assistant;
    }
    if (name == "plugins") {
        return BottomPanelTab::plugins;
    }
    return BottomPanelTab::diagnostics;
}

auto string_member(const boost::json::object& object, std::string_view name)
    -> std::optional<std::string_view> {
    const auto* value = object.if_contains(name);
    if (value == nullptr || !value->is_string()) {
        return std::nullopt;
    }
    return value->as_string();
}

void load_float(const boost::json::object& object,
                std::string_view name,
                float minimum,
                float maximum,
                float& target) {
    const auto* value = object.if_contains(name);
    if (value == nullptr || (!value->is_double() && !value->is_int64() && !value->is_uint64())) {
        return;
    }
    target = std::clamp(static_cast<float>(value->to_number<double>()), minimum, maximum);
}

void load_bool(const boost::json::object& object, std::string_view name, bool& target) {
    const auto* value = object.if_contains(name);
    if (value != nullptr && value->is_bool()) {
        target = value->as_bool();
    }
}

} // namespace

auto app_shell_state_path(const std::filesystem::path& repository) -> std::filesystem::path {
    return repository / ".liquid" / "liquify-state.json";
}

auto load_app_shell_state(const std::filesystem::path& repository) -> AppShellStateLoad {
    auto loaded = AppShellStateLoad{};
    const auto path = app_shell_state_path(repository);
    auto input = std::ifstream(path, std::ios::binary);
    if (!input) {
        return loaded;
    }
    const auto bytes = std::string(std::istreambuf_iterator<char>(input), {});
    boost::system::error_code error;
    const auto value = boost::json::parse(bytes, error);
    if (error || !value.is_object()) {
        loaded.error = "cannot read " + path.string() + ": invalid JSON";
        return loaded;
    }
    const auto& object = value.as_object();
    load_float(object, "sidebarWidth", 180.0F, 520.0F, loaded.state.sidebar_width);
    load_float(object, "bottomPanelHeight", 120.0F, 640.0F, loaded.state.bottom_panel_height);
    load_float(object, "editorSplitRatio", 0.25F, 0.75F, loaded.state.editor_split_ratio);
    load_bool(object, "sidebarVisible", loaded.state.sidebar_visible);
    load_bool(object, "bottomPanelVisible", loaded.state.bottom_panel_visible);
    load_bool(object, "editorSplit", loaded.state.editor_split);
    if (const auto name = string_member(object, "theme")) {
        loaded.state.theme = theme_from_name(*name);
    }
    if (const auto name = string_member(object, "sidebar")) {
        loaded.state.sidebar = sidebar_from_name(*name);
    }
    if (const auto name = string_member(object, "bottomPanel")) {
        loaded.state.bottom_panel = bottom_panel_from_name(*name);
    }
    return loaded;
}

auto save_app_shell_state(const std::filesystem::path& repository, const AppShellState& state)
    -> std::string {
    const auto path = app_shell_state_path(repository);
    std::error_code filesystem_error;
    std::filesystem::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error) {
        return "cannot create " + path.parent_path().string() + ": " + filesystem_error.message();
    }
    const auto object = boost::json::object{
        {"version", state_version},
        {"sidebarWidth", state.sidebar_width},
        {"bottomPanelHeight", state.bottom_panel_height},
        {"editorSplitRatio", state.editor_split_ratio},
        {"theme", theme_name(state.theme)},
        {"sidebar", sidebar_name(state.sidebar)},
        {"bottomPanel", bottom_panel_name(state.bottom_panel)},
        {"sidebarVisible", state.sidebar_visible},
        {"bottomPanelVisible", state.bottom_panel_visible},
        {"editorSplit", state.editor_split},
    };
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

} // namespace liquify
