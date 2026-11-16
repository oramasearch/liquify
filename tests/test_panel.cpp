// SPDX-License-Identifier: MIT

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "liquify/panel.hpp"

namespace {

TEST(PanelRegistry, DescribesEveryPanelAndItsCommands) {
    const auto descriptors = liquify::standard_panel_descriptors();

    EXPECT_EQ(descriptors.size(), 9U);
    for (const auto& descriptor : descriptors) {
        EXPECT_FALSE(descriptor.title.empty());
        EXPECT_FALSE(descriptor.icon.empty());
        EXPECT_FALSE(descriptor.empty_state.empty());
        EXPECT_EQ(liquify::panel_id_from_name(liquify::panel_id_name(descriptor.id)),
                  descriptor.id);
    }
    const auto build =
        std::ranges::find(descriptors, liquify::PanelId::build, &liquify::PanelDescriptor::id);
    ASSERT_NE(build, descriptors.end());
    EXPECT_EQ(build->default_placement, liquify::PanelPlacement::bottom);
    EXPECT_EQ(build->commands.size(), 3U);
}

TEST(PanelRegistry, MovesFloatsAndRedocksPanels) {
    auto panels = liquify::PanelRegistry{};

    panels.move(liquify::PanelId::diagnostics, liquify::PanelPlacement::sidebar);
    EXPECT_EQ(panels.layout(liquify::PanelId::diagnostics).placement,
              liquify::PanelPlacement::sidebar);
    EXPECT_EQ(panels.active(liquify::PanelPlacement::sidebar), liquify::PanelId::diagnostics);

    panels.move(liquify::PanelId::diagnostics, liquify::PanelPlacement::floating);
    panels.update_floating_rect(liquify::PanelId::diagnostics, {90.0F, 80.0F, 700.0F, 360.0F});
    EXPECT_EQ(panels.layout(liquify::PanelId::diagnostics).docked_placement,
              liquify::PanelPlacement::sidebar);
    EXPECT_FLOAT_EQ(panels.layout(liquify::PanelId::diagnostics).floating_rect.width, 700.0F);

    panels.redock(liquify::PanelId::diagnostics);
    EXPECT_EQ(panels.layout(liquify::PanelId::diagnostics).placement,
              liquify::PanelPlacement::sidebar);
}

TEST(PanelRegistry, HidingAnActivePanelSelectsAVisiblePeer) {
    auto panels = liquify::PanelRegistry{};
    panels.focus(liquify::PanelId::review);

    panels.set_visible(liquify::PanelId::review, false);

    EXPECT_NE(panels.active(liquify::PanelPlacement::sidebar), liquify::PanelId::review);
    EXPECT_TRUE(panels.active(liquify::PanelPlacement::sidebar).has_value());
}

TEST(PanelRegistry, RestoresMissingPanelsFromDefaults) {
    auto state = liquify::PanelRegistryState{};
    state.panels.push_back({liquify::PanelId::repository,
                            liquify::PanelPlacement::floating,
                            liquify::PanelPlacement::sidebar});

    const auto panels = liquify::PanelRegistry(std::move(state));

    EXPECT_EQ(panels.state().panels.size(), liquify::standard_panel_descriptors().size());
    EXPECT_EQ(panels.layout(liquify::PanelId::repository).placement,
              liquify::PanelPlacement::floating);
    EXPECT_EQ(panels.layout(liquify::PanelId::assistant).placement,
              liquify::PanelPlacement::bottom);
}

TEST(PanelRegistry, KeepsPanelRuntimeStateOutsideThePersistedLayout) {
    auto panels = liquify::PanelRegistry{};

    panels.set_panel_state(liquify::PanelId::review,
                           {.message = "Loading reviews", .error = {}, .busy = true});

    EXPECT_EQ(panels.panel_state(liquify::PanelId::review).message, "Loading reviews");
    EXPECT_TRUE(panels.panel_state(liquify::PanelId::review).busy);
    EXPECT_EQ(panels.state().panels.size(), liquify::standard_panel_descriptors().size());
}

TEST(PanelRegistry, PublishesProgressAndOnlyRequestsSupportedCancellation) {
    auto panels = liquify::PanelRegistry{};
    panels.begin_operation("build", liquify::PanelId::build, "Building repository");
    panels.update_operation("build", "Building repository", 2, 5);
    panels.begin_operation(
        "assistant", liquify::PanelId::assistant, "Assistant is responding", true);

    EXPECT_FALSE(panels.request_cancel("build"));
    EXPECT_TRUE(panels.request_cancel("assistant"));
    EXPECT_EQ(panels.take_cancel_requests(), std::vector<std::string>{"assistant"});
    ASSERT_NE(panels.latest_notification(), nullptr);
    EXPECT_EQ(panels.latest_notification()->panel, liquify::PanelId::assistant);

    panels.finish_operation(
        "assistant", liquify::PanelNotificationKind::success, "Assistant response stopped");
    EXPECT_FALSE(panels.latest_notification()->active);
    panels.dismiss_notification("assistant");
    EXPECT_EQ(panels.notifications().size(), 1U);
}

} // namespace
