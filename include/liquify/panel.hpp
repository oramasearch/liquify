// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace liquify {

enum class PanelId : std::uint8_t {
    repository,
    search,
    review,
    build,
    multiplayer,
    diagnostics,
    assistant,
    plugins,
    structural_editing,
};

enum class PanelPlacement : std::uint8_t {
    sidebar,
    bottom,
    editor_tab,
    floating,
};

enum class PanelNotificationKind : std::uint8_t {
    progress,
    information,
    success,
    error,
};

struct PanelCommand {
    std::string_view id;
    std::string_view title;
};

struct PanelDescriptor {
    PanelId id{PanelId::repository};
    std::string_view title;
    std::string_view icon;
    PanelPlacement default_placement{PanelPlacement::sidebar};
    std::span<const PanelCommand> commands;
    std::string_view empty_state;
};

struct PanelRect {
    float x = 120.0F;
    float y = 100.0F;
    float width = 480.0F;
    float height = 320.0F;
};

struct PanelLayout {
    PanelId id{PanelId::repository};
    PanelPlacement placement{PanelPlacement::sidebar};
    PanelPlacement docked_placement{PanelPlacement::sidebar};
    PanelRect floating_rect;
    std::size_t order{};
    bool visible{true};
};

struct PanelRegistryState {
    std::vector<PanelLayout> panels;
    PanelId active_sidebar{PanelId::repository};
    PanelId active_bottom{PanelId::diagnostics};
    PanelId active_editor{PanelId::structural_editing};
    std::optional<PanelId> focused;
};

struct PanelState {
    std::string message;
    std::string error;
    bool busy{};
};

struct PanelNotification {
    std::string id;
    PanelId panel{PanelId::repository};
    PanelNotificationKind kind{PanelNotificationKind::information};
    std::string message;
    std::size_t completed{};
    std::size_t total{};
    bool active{};
    bool cancellable{};
    bool cancel_requested{};
};

[[nodiscard]] auto panel_id_name(PanelId id) -> std::string_view;
[[nodiscard]] auto panel_id_from_name(std::string_view name) -> std::optional<PanelId>;
[[nodiscard]] auto panel_placement_name(PanelPlacement placement) -> std::string_view;
[[nodiscard]] auto panel_placement_from_name(std::string_view name)
    -> std::optional<PanelPlacement>;
[[nodiscard]] auto standard_panel_descriptors() -> std::span<const PanelDescriptor>;
[[nodiscard]] auto default_panel_registry_state() -> PanelRegistryState;

class PanelRegistry {
public:
    PanelRegistry();
    explicit PanelRegistry(PanelRegistryState state);

    void restore(PanelRegistryState state);
    void reset();
    [[nodiscard]] auto state() const -> const PanelRegistryState&;
    [[nodiscard]] auto descriptor(PanelId id) const -> const PanelDescriptor&;
    [[nodiscard]] auto layout(PanelId id) const -> const PanelLayout&;
    [[nodiscard]] auto panels_at(PanelPlacement placement) const -> std::vector<PanelId>;
    [[nodiscard]] auto active(PanelPlacement placement) const -> std::optional<PanelId>;

    void focus(PanelId id);
    void clear_focus_request();
    [[nodiscard]] auto focus_requested(PanelId id) const -> bool;
    void set_visible(PanelId id, bool visible);
    void move(PanelId id, PanelPlacement placement);
    void redock(PanelId id);
    void update_floating_rect(PanelId id, PanelRect rect);

    void set_panel_state(PanelId id, PanelState state);
    [[nodiscard]] auto panel_state(PanelId id) const -> const PanelState&;

    void
    begin_operation(std::string id, PanelId panel, std::string message, bool cancellable = false);
    void update_operation(std::string_view id,
                          std::string message,
                          std::size_t completed = 0,
                          std::size_t total = 0);
    void finish_operation(std::string_view id, PanelNotificationKind kind, std::string message);
    [[nodiscard]] auto notifications() const -> std::span<const PanelNotification>;
    [[nodiscard]] auto latest_notification() const -> const PanelNotification*;
    [[nodiscard]] auto request_cancel(std::string_view id) -> bool;
    [[nodiscard]] auto take_cancel_requests() -> std::vector<std::string>;
    void dismiss_notification(std::string_view id);

private:
    [[nodiscard]] auto mutable_layout(PanelId id) -> PanelLayout&;
    [[nodiscard]] auto mutable_notification(std::string_view id) -> PanelNotification*;
    void normalize();
    void select_fallback(PanelPlacement placement);

    PanelRegistryState registry_state_;
    std::vector<PanelState> panel_states_;
    std::vector<PanelNotification> notifications_;
    bool focus_requested_{};
};

} // namespace liquify
