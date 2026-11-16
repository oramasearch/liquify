// SPDX-License-Identifier: MIT

#include "liquify/panel.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace liquify {
namespace {

constexpr auto repository_commands = std::array<PanelCommand, 0>{};
constexpr auto search_commands = std::array<PanelCommand, 0>{};
constexpr auto review_commands = std::to_array<PanelCommand>({
    {"review.refresh", "Review: Refresh"},
    {"review.next-hunk", "Review: Next hunk (Alt+Down)"},
    {"review.previous-hunk", "Review: Previous hunk (Alt+Up)"},
    {"review.next-comment", "Review: Next comment (Alt+C)"},
    {"review.new-comment", "Review: New comment (Primary+Alt+C)"},
    {"review.approve", "Review: Approve (Primary+Alt+A)"},
});
constexpr auto build_commands = std::to_array<PanelCommand>({
    {"build.start", "Build (F7)"},
    {"build.test", "Run all tests (F8)"},
    {"build.push", "Push (F9)"},
});
constexpr auto multiplayer_commands = std::array<PanelCommand, 0>{};
constexpr auto diagnostics_commands = std::array<PanelCommand, 0>{};
constexpr auto assistant_commands = std::to_array<PanelCommand>({
    {"assistant.ask", "Assistant: Ask (Primary+Shift+A)"},
    {"assistant.explain", "Assistant: Explain diagnostic (Primary+Shift+E)"},
    {"assistant.propose", "Assistant: Propose edit (Primary+Shift+I)"},
    {"assistant.accept", "Assistant: Accept edit (Primary+Enter)"},
    {"assistant.reject", "Assistant: Reject edit (Primary+Backspace)"},
    {"assistant.toggle", "Assistant: Toggle (Primary+Shift+L)"},
    {"assistant.context", "Assistant: What will be sent (Primary+Shift+C)"},
});
constexpr auto plugin_commands = std::array<PanelCommand, 0>{};
constexpr auto structural_commands = std::to_array<PanelCommand>({
    {"structural.expand", "Selection: Expand (Primary+Shift+Right)"},
    {"structural.shrink", "Selection: Shrink (Primary+Shift+Left)"},
    {"structural.definition",
     "Selection: Enclosing definition (Primary+Shift+D)"},
    {"structural.references", "Selection: Symbol references (Primary+Alt+F)"},
    {"structural.rename", "Refactor: Rename (Primary+Shift+R)"},
    {"structural.extract", "Refactor: Extract function (Primary+Shift+X)"},
});

constexpr auto descriptors = std::to_array<PanelDescriptor>({
    {PanelId::repository, "Repository", "R", PanelPlacement::sidebar,
     repository_commands, "Open a liquid repository to browse files."},
    {PanelId::search, "Search", "S", PanelPlacement::sidebar, search_commands,
     "Open a repository to search its files."},
    {PanelId::review, "Review", "V", PanelPlacement::sidebar, review_commands,
     "No reviews yet. Push a change, then request a review."},
    {PanelId::build, "Build, test and push", "B", PanelPlacement::bottom,
     build_commands, "No build, test, or push activity yet."},
    {PanelId::multiplayer, "Multiplayer", "M", PanelPlacement::sidebar,
     multiplayer_commands, "No one else is connected to this repository."},
    {PanelId::diagnostics, "Diagnostics", "D", PanelPlacement::bottom,
     diagnostics_commands, "No diagnostics to show."},
    {PanelId::assistant, "Assistant", "A", PanelPlacement::bottom,
     assistant_commands, "Open a repository to use the assistant."},
    {PanelId::plugins, "Plugins", "P", PanelPlacement::bottom, plugin_commands,
     "No plugin panels are registered."},
    {PanelId::structural_editing, "Structural editing", "E",
     PanelPlacement::editor_tab, structural_commands,
     "No structural edit preview is open."},
});

auto panel_index(PanelId id) -> std::size_t {
  return static_cast<std::size_t>(id);
}

auto active_for(const PanelRegistryState &state, PanelPlacement placement)
    -> PanelId {
  switch (placement) {
  case PanelPlacement::sidebar:
    return state.active_sidebar;
  case PanelPlacement::bottom:
    return state.active_bottom;
  case PanelPlacement::editor_tab:
    return state.active_editor;
  case PanelPlacement::floating:
    return state.focused.value_or(PanelId::repository);
  }
  return PanelId::repository;
}

void set_active(PanelRegistryState &state, PanelPlacement placement,
                PanelId id) {
  switch (placement) {
  case PanelPlacement::sidebar:
    state.active_sidebar = id;
    break;
  case PanelPlacement::bottom:
    state.active_bottom = id;
    break;
  case PanelPlacement::editor_tab:
    state.active_editor = id;
    break;
  case PanelPlacement::floating:
    break;
  }
}

} // namespace

auto panel_id_name(PanelId id) -> std::string_view {
  switch (id) {
  case PanelId::repository:
    return "repository";
  case PanelId::search:
    return "search";
  case PanelId::review:
    return "review";
  case PanelId::build:
    return "build";
  case PanelId::multiplayer:
    return "multiplayer";
  case PanelId::diagnostics:
    return "diagnostics";
  case PanelId::assistant:
    return "assistant";
  case PanelId::plugins:
    return "plugins";
  case PanelId::structural_editing:
    return "structural-editing";
  }
  return "repository";
}

auto panel_id_from_name(std::string_view name) -> std::optional<PanelId> {
  const auto found = std::ranges::find(descriptors, name,
                                       [](const PanelDescriptor &descriptor) {
                                         return panel_id_name(descriptor.id);
                                       });
  if (found == descriptors.end()) {
    return std::nullopt;
  }
  return found->id;
}

auto panel_placement_name(PanelPlacement placement) -> std::string_view {
  switch (placement) {
  case PanelPlacement::sidebar:
    return "sidebar";
  case PanelPlacement::bottom:
    return "bottom";
  case PanelPlacement::editor_tab:
    return "editor-tab";
  case PanelPlacement::floating:
    return "floating";
  }
  return "sidebar";
}

auto panel_placement_from_name(std::string_view name)
    -> std::optional<PanelPlacement> {
  if (name == "sidebar") {
    return PanelPlacement::sidebar;
  }
  if (name == "bottom") {
    return PanelPlacement::bottom;
  }
  if (name == "editor-tab") {
    return PanelPlacement::editor_tab;
  }
  if (name == "floating") {
    return PanelPlacement::floating;
  }
  return std::nullopt;
}

auto standard_panel_descriptors() -> std::span<const PanelDescriptor> {
  return descriptors;
}

auto default_panel_registry_state() -> PanelRegistryState {
  auto state = PanelRegistryState{};
  state.panels.reserve(descriptors.size());
  auto orders = std::array<std::size_t, 4>{};
  for (const auto &descriptor : descriptors) {
    const auto placement_index =
        static_cast<std::size_t>(descriptor.default_placement);
    state.panels.push_back({descriptor.id,
                            descriptor.default_placement,
                            descriptor.default_placement,
                            {},
                            orders[placement_index]++,
                            descriptor.id != PanelId::structural_editing});
  }
  return state;
}

PanelRegistry::PanelRegistry()
    : PanelRegistry(default_panel_registry_state()) {}

PanelRegistry::PanelRegistry(PanelRegistryState state) {
  restore(std::move(state));
}

void PanelRegistry::restore(PanelRegistryState state) {
  registry_state_ = std::move(state);
  panel_states_.resize(descriptors.size());
  focus_requested_ = false;
  normalize();
}

void PanelRegistry::reset() { restore(default_panel_registry_state()); }

auto PanelRegistry::state() const -> const PanelRegistryState & {
  return registry_state_;
}

auto PanelRegistry::descriptor(PanelId id) const -> const PanelDescriptor & {
  return descriptors.at(panel_index(id));
}

auto PanelRegistry::layout(PanelId id) const -> const PanelLayout & {
  const auto found =
      std::ranges::find(registry_state_.panels, id, &PanelLayout::id);
  if (found == registry_state_.panels.end()) {
    throw std::out_of_range("panel is not registered");
  }
  return *found;
}

auto PanelRegistry::panels_at(PanelPlacement placement) const
    -> std::vector<PanelId> {
  auto layouts = std::vector<const PanelLayout *>{};
  for (const auto &item : registry_state_.panels) {
    if (item.placement == placement) {
      layouts.push_back(&item);
    }
  }
  std::ranges::sort(layouts, {},
                    [](const PanelLayout *item) { return item->order; });
  auto output = std::vector<PanelId>{};
  output.reserve(layouts.size());
  for (const auto *item : layouts) {
    output.push_back(item->id);
  }
  return output;
}

auto PanelRegistry::active(PanelPlacement placement) const
    -> std::optional<PanelId> {
  const auto selected = active_for(registry_state_, placement);
  const auto &selected_layout = layout(selected);
  if (selected_layout.placement == placement && selected_layout.visible) {
    return selected;
  }
  for (const auto id : panels_at(placement)) {
    if (layout(id).visible) {
      return id;
    }
  }
  return std::nullopt;
}

void PanelRegistry::focus(PanelId id) {
  auto &item = mutable_layout(id);
  item.visible = true;
  set_active(registry_state_, item.placement, id);
  registry_state_.focused = id;
  focus_requested_ = true;
}

void PanelRegistry::clear_focus_request() { focus_requested_ = false; }

auto PanelRegistry::focus_requested(PanelId id) const -> bool {
  return focus_requested_ && registry_state_.focused == id;
}

void PanelRegistry::set_visible(PanelId id, bool visible) {
  auto &item = mutable_layout(id);
  item.visible = visible;
  if (!visible) {
    select_fallback(item.placement);
  }
}

void PanelRegistry::move(PanelId id, PanelPlacement placement) {
  auto &item = mutable_layout(id);
  const auto previous = item.placement;
  if (placement == PanelPlacement::floating &&
      previous != PanelPlacement::floating) {
    item.docked_placement = previous;
  } else if (placement != PanelPlacement::floating) {
    item.docked_placement = placement;
  }
  const auto peers = panels_at(placement);
  item.placement = placement;
  item.order = peers.size();
  item.visible = true;
  select_fallback(previous);
  focus(id);
}

void PanelRegistry::redock(PanelId id) {
  const auto target = layout(id).docked_placement == PanelPlacement::floating
                          ? descriptor(id).default_placement
                          : layout(id).docked_placement;
  move(id, target);
}

void PanelRegistry::update_floating_rect(PanelId id, PanelRect rect) {
  auto &item = mutable_layout(id);
  if (item.placement == PanelPlacement::floating) {
    item.floating_rect = rect;
  }
}

void PanelRegistry::set_panel_state(PanelId id, PanelState state) {
  panel_states_.at(panel_index(id)) = std::move(state);
}

auto PanelRegistry::panel_state(PanelId id) const -> const PanelState & {
  return panel_states_.at(panel_index(id));
}

void PanelRegistry::begin_operation(std::string id, PanelId panel,
                                    std::string message, bool cancellable) {
  const auto found =
      std::ranges::find(notifications_, id, &PanelNotification::id);
  if (found != notifications_.end()) {
    notifications_.erase(found);
  }
  notifications_.push_back({std::move(id), panel,
                            PanelNotificationKind::progress, std::move(message),
                            0, 0, true, cancellable, false});
}

void PanelRegistry::update_operation(std::string_view id, std::string message,
                                     std::size_t completed, std::size_t total) {
  auto *notification = mutable_notification(id);
  if (notification == nullptr || !notification->active) {
    return;
  }
  notification->message = std::move(message);
  notification->completed = completed;
  notification->total = total;
}

void PanelRegistry::finish_operation(std::string_view id,
                                     PanelNotificationKind kind,
                                     std::string message) {
  const auto operation_id = std::string(id);
  auto *notification = mutable_notification(operation_id);
  if (notification == nullptr) {
    return;
  }
  notification->kind = kind;
  notification->message = std::move(message);
  notification->active = false;
  notification->cancellable = false;
  notification->cancel_requested = false;
  if (notification != &notifications_.back()) {
    auto finished = std::move(*notification);
    notifications_.erase(std::ranges::find(notifications_, operation_id,
                                           &PanelNotification::id));
    notifications_.push_back(std::move(finished));
  }
}

auto PanelRegistry::notifications() const
    -> std::span<const PanelNotification> {
  return notifications_;
}

auto PanelRegistry::latest_notification() const -> const PanelNotification * {
  return notifications_.empty() ? nullptr : &notifications_.back();
}

auto PanelRegistry::request_cancel(std::string_view id) -> bool {
  auto *notification = mutable_notification(id);
  if (notification == nullptr || !notification->active ||
      !notification->cancellable) {
    return false;
  }
  notification->cancel_requested = true;
  return true;
}

auto PanelRegistry::take_cancel_requests() -> std::vector<std::string> {
  auto output = std::vector<std::string>{};
  for (auto &notification : notifications_) {
    if (!notification.cancel_requested) {
      continue;
    }
    output.push_back(notification.id);
    notification.cancel_requested = false;
  }
  return output;
}

void PanelRegistry::dismiss_notification(std::string_view id) {
  const auto found =
      std::ranges::find(notifications_, id, &PanelNotification::id);
  if (found != notifications_.end() && !found->active) {
    notifications_.erase(found);
  }
}

auto PanelRegistry::mutable_layout(PanelId id) -> PanelLayout & {
  const auto found =
      std::ranges::find(registry_state_.panels, id, &PanelLayout::id);
  if (found == registry_state_.panels.end()) {
    throw std::out_of_range("panel is not registered");
  }
  return *found;
}

auto PanelRegistry::mutable_notification(std::string_view id)
    -> PanelNotification * {
  const auto found =
      std::ranges::find(notifications_, id, &PanelNotification::id);
  return found == notifications_.end() ? nullptr : &*found;
}

void PanelRegistry::normalize() {
  auto normalized = default_panel_registry_state();
  for (auto &target : normalized.panels) {
    const auto found =
        std::ranges::find(registry_state_.panels, target.id, &PanelLayout::id);
    if (found != registry_state_.panels.end()) {
      target = *found;
    }
  }
  normalized.active_sidebar = registry_state_.active_sidebar;
  normalized.active_bottom = registry_state_.active_bottom;
  normalized.active_editor = registry_state_.active_editor;
  normalized.focused = registry_state_.focused;
  registry_state_ = std::move(normalized);
  select_fallback(PanelPlacement::sidebar);
  select_fallback(PanelPlacement::bottom);
  select_fallback(PanelPlacement::editor_tab);
}

void PanelRegistry::select_fallback(PanelPlacement placement) {
  if (placement == PanelPlacement::floating) {
    return;
  }
  const auto selected = active_for(registry_state_, placement);
  const auto &selected_layout = layout(selected);
  if (selected_layout.placement == placement && selected_layout.visible) {
    return;
  }
  for (const auto id : panels_at(placement)) {
    if (layout(id).visible) {
      set_active(registry_state_, placement, id);
      return;
    }
  }
}

} // namespace liquify
