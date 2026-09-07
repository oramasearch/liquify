#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "liquify/input.hpp"

namespace liquify {
namespace {

TEST(InputState, TracksAndCommitsUtf8Composition) {
  auto input = InputState{};

  input.update_composition("にほ", 1, 1);
  ASSERT_TRUE(input.composition().active());
  EXPECT_EQ(input.composition().text, "にほ");
  EXPECT_EQ(input.composition().selection_start, 1U);
  EXPECT_EQ(input.composition().selection_length, 1U);

  input.update_composition("日本語", 2, 4);
  EXPECT_EQ(input.composition().selection_start, 2U);
  EXPECT_EQ(input.composition().selection_length, 1U);
  EXPECT_EQ(utf8_byte_offset(input.composition().text, 2), 6U);

  EXPECT_EQ(input.commit_text("日本語"), "日本語");
  EXPECT_FALSE(input.composition().active());
}

TEST(InputState, CancelsCompositionForDeadKeyOrFocusLoss) {
  auto input = InputState{};
  input.update_composition("´", -1, -1);
  ASSERT_TRUE(input.composition().active());

  input.cancel_composition();
  EXPECT_FALSE(input.composition().active());

  input.update_composition("´", 0, 1);
  input.update_composition({}, 0, 0);
  EXPECT_FALSE(input.composition().active());
}

TEST(InputState, RoutesDropsToEditorAndRepositoryTree) {
  auto input = InputState{};
  const auto tree = InputRectangle{48.0F, 0.0F, 260.0F, 720.0F};
  const auto editor = InputRectangle{312.0F, 0.0F, 900.0F, 500.0F};

  EXPECT_EQ(input.update_drop_target({100.0F, 200.0F}, tree, editor),
            DropTarget::repository_tree);
  EXPECT_EQ(input.update_drop_target({500.0F, 200.0F}, tree, editor),
            DropTarget::editor);
  EXPECT_EQ(input.update_drop_target({1.0F, 799.0F}, tree, editor),
            DropTarget::none);

  input.clear_drop_target();
  EXPECT_EQ(input.drop_target(), DropTarget::none);
}

TEST(InputState, ProvidesRichAndPlainClipboardRepresentations) {
  const auto payload =
      make_clipboard_payload("if (a < b)\n    return a & b;", "src/main.cpp");
  EXPECT_EQ(payload.plain_text, "if (a < b)\n    return a & b;");
  EXPECT_EQ(payload.html, "<pre data-liquify-path=\"src/main.cpp\">if (a &lt; "
                          "b)\n    return a &amp; "
                          "b;</pre>");
  EXPECT_EQ(preferred_clipboard_text(payload.plain_text, payload.html),
            payload.plain_text);
}

TEST(InputState, ConvertsRichOnlyClipboardDataToEditorText) {
  const auto pasted = preferred_clipboard_text(
      std::nullopt, "<div>first &amp; second</div><div>&lt;third&gt;</div>");
  ASSERT_TRUE(pasted);
  EXPECT_EQ(*pasted, "first & second\n<third>");
  EXPECT_FALSE(preferred_clipboard_text(std::nullopt, std::nullopt));
}

TEST(InputState, RemovesTransitionDurationWhenMotionIsReduced) {
  EXPECT_FLOAT_EQ(transition_duration(0.18F, {}), 0.18F);
  EXPECT_FLOAT_EQ(transition_duration(0.18F, {.reduced_motion = true}), 0.0F);
  EXPECT_FLOAT_EQ(transition_duration(-1.0F, {}), 0.0F);
}

TEST(InputState, PublishesNamedAccessibilityControls) {
  const auto nodes = accessibility_nodes();
  const auto editor =
      std::ranges::find(nodes, "editor.surface", &AccessibilityNode::id);
  const auto repository =
      std::ranges::find(nodes, "panel.repository", &AccessibilityNode::id);
  const auto settings =
      std::ranges::find(nodes, "panel.settings", &AccessibilityNode::id);
  const auto contrast =
      std::ranges::find(nodes, "status.contrast", &AccessibilityNode::id);

  ASSERT_NE(editor, nodes.end());
  EXPECT_EQ(editor->role, AccessibilityRole::text_area);
  EXPECT_NE(repository, nodes.end());
  EXPECT_NE(settings, nodes.end());
  EXPECT_NE(contrast, nodes.end());
}

} // namespace
} // namespace liquify
