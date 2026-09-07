#include "liquify/app_state.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>

#include <boost/json.hpp>

namespace liquify {
namespace {

constexpr auto state_version = 3;

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

auto theme_from_name(std::string_view name) -> ThemePreference {
  if (name == "light") {
    return ThemePreference::light;
  }
  if (name == "dark") {
    return ThemePreference::dark;
  }
  return ThemePreference::system;
}

auto string_member(const boost::json::object &object, std::string_view name)
    -> std::optional<std::string_view> {
  const auto *value = object.if_contains(name);
  if (value == nullptr || !value->is_string()) {
    return std::nullopt;
  }
  return value->as_string();
}

void load_float(const boost::json::object &object, std::string_view name,
                float minimum, float maximum, float &target) {
  const auto *value = object.if_contains(name);
  if (value == nullptr ||
      (!value->is_double() && !value->is_int64() && !value->is_uint64())) {
    return;
  }
  target = std::clamp(static_cast<float>(value->to_number<double>()), minimum,
                      maximum);
}

void load_size(const boost::json::object &object, std::string_view name,
               std::size_t &target) {
  const auto *value = object.if_contains(name);
  if (value == nullptr || (!value->is_int64() && !value->is_uint64())) {
    return;
  }
  target = value->to_number<std::size_t>();
}

void load_bool(const boost::json::object &object, std::string_view name,
               bool &target) {
  const auto *value = object.if_contains(name);
  if (value != nullptr && value->is_bool()) {
    target = value->as_bool();
  }
}

auto panel_layout_json(const PanelLayout &layout) -> boost::json::object {
  return {
      {"id", panel_id_name(layout.id)},
      {"placement", panel_placement_name(layout.placement)},
      {"dockedPlacement", panel_placement_name(layout.docked_placement)},
      {"visible", layout.visible},
      {"order", layout.order},
      {"x", layout.floating_rect.x},
      {"y", layout.floating_rect.y},
      {"width", layout.floating_rect.width},
      {"height", layout.floating_rect.height},
  };
}

auto panel_registry_json(const PanelRegistryState &state)
    -> boost::json::object {
  auto panels = boost::json::array{};
  panels.reserve(state.panels.size());
  for (const auto &panel : state.panels) {
    panels.push_back(panel_layout_json(panel));
  }
  auto object = boost::json::object{
      {"items", std::move(panels)},
      {"activeSidebar", panel_id_name(state.active_sidebar)},
      {"activeBottom", panel_id_name(state.active_bottom)},
      {"activeEditor", panel_id_name(state.active_editor)},
  };
  if (state.focused) {
    object["focused"] = panel_id_name(*state.focused);
  }
  return object;
}

void load_panel_layout(const boost::json::object &object, PanelLayout &layout) {
  if (const auto placement = string_member(object, "placement")) {
    if (const auto parsed = panel_placement_from_name(*placement)) {
      layout.placement = *parsed;
    }
  }
  if (const auto placement = string_member(object, "dockedPlacement")) {
    if (const auto parsed = panel_placement_from_name(*placement)) {
      layout.docked_placement = *parsed;
    }
  }
  load_bool(object, "visible", layout.visible);
  load_size(object, "order", layout.order);
  load_float(object, "x", -10'000.0F, 10'000.0F, layout.floating_rect.x);
  load_float(object, "y", -10'000.0F, 10'000.0F, layout.floating_rect.y);
  load_float(object, "width", 240.0F, 4'000.0F, layout.floating_rect.width);
  load_float(object, "height", 140.0F, 4'000.0F, layout.floating_rect.height);
}

void load_active_panel(const boost::json::object &object,
                       std::string_view member, PanelId &target) {
  if (const auto name = string_member(object, member)) {
    if (const auto id = panel_id_from_name(*name)) {
      target = *id;
    }
  }
}

void load_panel_registry(const boost::json::object &object,
                         PanelRegistryState &state) {
  state = default_panel_registry_state();
  const auto *items = object.if_contains("items");
  if (items != nullptr && items->is_array()) {
    for (const auto &value : items->as_array()) {
      if (!value.is_object()) {
        continue;
      }
      const auto &panel_object = value.as_object();
      const auto name = string_member(panel_object, "id");
      const auto id = name ? panel_id_from_name(*name) : std::nullopt;
      if (!id) {
        continue;
      }
      const auto found = std::ranges::find(state.panels, *id, &PanelLayout::id);
      if (found != state.panels.end()) {
        load_panel_layout(panel_object, *found);
      }
    }
  }
  load_active_panel(object, "activeSidebar", state.active_sidebar);
  load_active_panel(object, "activeBottom", state.active_bottom);
  load_active_panel(object, "activeEditor", state.active_editor);
  if (const auto name = string_member(object, "focused")) {
    state.focused = panel_id_from_name(*name);
  }
  const auto registry = PanelRegistry(std::move(state));
  state = registry.state();
}

void load_legacy_panel_selection(const boost::json::object &object,
                                 PanelRegistryState &state) {
  auto registry = PanelRegistry(state);
  if (const auto name = string_member(object, "sidebar")) {
    const auto id = panel_id_from_name(*name);
    if (id && registry.layout(*id).placement == PanelPlacement::sidebar) {
      registry.focus(*id);
    } else if (id == PanelId::build) {
      registry.focus(PanelId::build);
    }
  }
  if (const auto name = string_member(object, "bottomPanel")) {
    if (const auto id = panel_id_from_name(*name)) {
      registry.focus(*id);
    }
  }
  registry.clear_focus_request();
  state = registry.state();
}

} // namespace

auto app_shell_state_path(const std::filesystem::path &repository)
    -> std::filesystem::path {
  return repository / ".liquid" / "liquify-state.json";
}

auto load_app_shell_state(const std::filesystem::path &repository)
    -> AppShellStateLoad {
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
  const auto &object = value.as_object();
  load_float(object, "sidebarWidth", 180.0F, 520.0F,
             loaded.state.sidebar_width);
  load_float(object, "bottomPanelHeight", 120.0F, 640.0F,
             loaded.state.bottom_panel_height);
  load_float(object, "editorSplitRatio", 0.25F, 0.75F,
             loaded.state.editor_split_ratio);
  load_bool(object, "sidebarVisible", loaded.state.sidebar_visible);
  load_bool(object, "bottomPanelVisible", loaded.state.bottom_panel_visible);
  load_bool(object, "editorSplit", loaded.state.editor_split);
  load_bool(object, "highContrast", loaded.state.high_contrast);
  if (const auto name = string_member(object, "theme")) {
    loaded.state.theme = theme_from_name(*name);
  }
  const auto *panels = object.if_contains("panels");
  if (panels != nullptr && panels->is_object()) {
    load_panel_registry(panels->as_object(), loaded.state.panel_layout);
  } else {
    load_legacy_panel_selection(object, loaded.state.panel_layout);
  }
  return loaded;
}

auto save_app_shell_state(const std::filesystem::path &repository,
                          const AppShellState &state) -> std::string {
  const auto path = app_shell_state_path(repository);
  std::error_code filesystem_error;
  std::filesystem::create_directories(path.parent_path(), filesystem_error);
  if (filesystem_error) {
    return "cannot create " + path.parent_path().string() + ": " +
           filesystem_error.message();
  }
  const auto object = boost::json::object{
      {"version", state_version},
      {"sidebarWidth", state.sidebar_width},
      {"bottomPanelHeight", state.bottom_panel_height},
      {"editorSplitRatio", state.editor_split_ratio},
      {"theme", theme_name(state.theme)},
      {"panels", panel_registry_json(state.panel_layout)},
      {"highContrast", state.high_contrast},
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

void reset_app_shell_layout(AppShellState &state) {
  const auto theme = state.theme;
  const auto high_contrast = state.high_contrast;
  state = AppShellState{};
  state.theme = theme;
  state.high_contrast = high_contrast;
}

} // namespace liquify
