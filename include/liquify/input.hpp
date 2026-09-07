#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace liquify {

struct TextComposition {
  std::string text;
  std::size_t selection_start{};
  std::size_t selection_length{};

  [[nodiscard]] auto active() const -> bool;
};

struct InputPoint {
  float x{};
  float y{};
};

struct InputRectangle {
  float x{};
  float y{};
  float width{};
  float height{};

  [[nodiscard]] auto contains(InputPoint point) const -> bool;
};

enum class DropTarget : std::uint8_t {
  none,
  repository_tree,
  editor,
};

class InputState {
public:
  void update_composition(std::string text, int selection_start,
                          int selection_length);
  [[nodiscard]] auto commit_text(std::string text) -> std::string;
  void cancel_composition();

  [[nodiscard]] auto composition() const -> const TextComposition &;
  [[nodiscard]] auto update_drop_target(InputPoint point,
                                        InputRectangle repository_tree,
                                        InputRectangle editor) -> DropTarget;
  void clear_drop_target();
  [[nodiscard]] auto drop_target() const -> DropTarget;

private:
  TextComposition composition_;
  DropTarget drop_target_{DropTarget::none};
};

struct ClipboardPayload {
  std::string plain_text;
  std::string html;
};

[[nodiscard]] auto make_clipboard_payload(std::string_view text,
                                          std::string_view source_path)
    -> ClipboardPayload;
[[nodiscard]] auto html_to_plain_text(std::string_view html) -> std::string;
[[nodiscard]] auto
preferred_clipboard_text(std::optional<std::string_view> plain_text,
                         std::optional<std::string_view> html)
    -> std::optional<std::string>;
[[nodiscard]] auto utf8_codepoint_count(std::string_view text) -> std::size_t;
[[nodiscard]] auto utf8_byte_offset(std::string_view text,
                                    std::size_t codepoint) -> std::size_t;

struct AccessibilitySettings {
  bool high_contrast{};
  bool reduced_motion{};
};

[[nodiscard]] auto transition_duration(float duration,
                                       AccessibilitySettings settings) -> float;

enum class AccessibilityRole : std::uint8_t {
  group,
  button,
  text_area,
};

struct AccessibilityNode {
  std::string_view id;
  std::string_view label;
  std::string_view help;
  AccessibilityRole role{AccessibilityRole::group};
};

[[nodiscard]] auto accessibility_nodes() -> std::span<const AccessibilityNode>;

} // namespace liquify
