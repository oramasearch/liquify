#include "liquify/input.hpp"

#include <algorithm>
#include <array>

namespace liquify {
namespace {

constexpr auto nodes = std::to_array<AccessibilityNode>({
    {"activity.repository", "Repository", "Show the repository panel",
     AccessibilityRole::button},
    {"activity.search", "Search", "Show repository search",
     AccessibilityRole::button},
    {"activity.review", "Review", "Show review controls",
     AccessibilityRole::button},
    {"activity.build", "Build", "Show build, test, and deploy",
     AccessibilityRole::button},
    {"activity.multiplayer", "Multiplayer", "Show collaboration activity",
     AccessibilityRole::button},
    {"panel.repository", "Repository panel", "Browse and open repository files",
     AccessibilityRole::group},
    {"panel.search", "Search panel", "Search and replace in the repository",
     AccessibilityRole::group},
    {"panel.review", "Review panel", "Read and update the active review",
     AccessibilityRole::group},
    {"panel.build", "Build panel", "Run and inspect build, test, and deploy",
     AccessibilityRole::group},
    {"panel.diagnostics", "Diagnostics panel", "Inspect editor diagnostics",
     AccessibilityRole::group},
    {"panel.assistant", "Assistant panel",
     "Inspect and control the optional assistant", AccessibilityRole::group},
    {"panel.plugins", "Plugins panel", "Inspect registered plugin panels",
     AccessibilityRole::group},
    {"panel.structural", "Structural editing panel",
     "Inspect structural edit previews", AccessibilityRole::group},
    {"panel.settings", "Settings",
     "Search and update user or repository settings", AccessibilityRole::group},
    {"editor.mode", "Editing mode", "Switch standard and modal editing",
     AccessibilityRole::button},
    {"editor.save", "Save", "Save the active editor buffer",
     AccessibilityRole::button},
    {"editor.build", "Build", "Build the active repository",
     AccessibilityRole::button},
    {"editor.test", "Tests", "Run repository tests", AccessibilityRole::button},
    {"editor.deploy", "Deploy", "Deploy saved repository changes",
     AccessibilityRole::button},
    {"editor.split", "Editor split", "Show or hide the secondary editor",
     AccessibilityRole::button},
    {"editor.panel", "Bottom panel", "Show or hide the bottom panel",
     AccessibilityRole::button},
    {"editor.surface", "Source editor", "Edit the active source file",
     AccessibilityRole::text_area},
    {"status.theme", "Color theme", "Cycle the system, light, and dark themes",
     AccessibilityRole::button},
    {"status.contrast", "High contrast", "Toggle the high-contrast theme",
     AccessibilityRole::button},
});

auto escaped_html(std::string_view text) -> std::string {
  auto output = std::string{};
  output.reserve(text.size());
  for (const auto character : text) {
    switch (character) {
    case '&':
      output += "&amp;";
      break;
    case '<':
      output += "&lt;";
      break;
    case '>':
      output += "&gt;";
      break;
    case '"':
      output += "&quot;";
      break;
    case '\'':
      output += "&#39;";
      break;
    default:
      output.push_back(character);
      break;
    }
  }
  return output;
}

void append_entity(std::string &output, std::string_view entity) {
  if (entity == "amp") {
    output.push_back('&');
  } else if (entity == "lt") {
    output.push_back('<');
  } else if (entity == "gt") {
    output.push_back('>');
  } else if (entity == "quot") {
    output.push_back('"');
  } else if (entity == "apos" || entity == "#39") {
    output.push_back('\'');
  } else if (entity == "nbsp") {
    output.push_back(' ');
  } else {
    output.push_back('&');
    output.append(entity);
    output.push_back(';');
  }
}

auto tag_name(std::string_view tag) -> std::string_view {
  while (!tag.empty() && (tag.front() == '/' || tag.front() == ' ')) {
    tag.remove_prefix(1);
  }
  const auto end = tag.find_first_of(" \t\r\n/");
  return tag.substr(0, end);
}

void append_line_break(std::string &output) {
  if (!output.empty() && output.back() != '\n') {
    output.push_back('\n');
  }
}

} // namespace

auto TextComposition::active() const -> bool { return !text.empty(); }

auto InputRectangle::contains(InputPoint point) const -> bool {
  return width > 0.0F && height > 0.0F && point.x >= x && point.y >= y &&
         point.x < x + width && point.y < y + height;
}

void InputState::update_composition(std::string text, int selection_start,
                                    int selection_length) {
  if (text.empty()) {
    cancel_composition();
    return;
  }
  const auto characters = utf8_codepoint_count(text);
  const auto start =
      selection_start < 0 ? 0U : static_cast<std::size_t>(selection_start);
  const auto length =
      selection_length < 0 ? 0U : static_cast<std::size_t>(selection_length);
  composition_.text = std::move(text);
  composition_.selection_start = std::min(start, characters);
  composition_.selection_length =
      std::min(length, characters - composition_.selection_start);
}

auto InputState::commit_text(std::string text) -> std::string {
  cancel_composition();
  return text;
}

void InputState::cancel_composition() { composition_ = {}; }

auto InputState::composition() const -> const TextComposition & {
  return composition_;
}

auto InputState::update_drop_target(InputPoint point,
                                    InputRectangle repository_tree,
                                    InputRectangle editor) -> DropTarget {
  if (editor.contains(point)) {
    drop_target_ = DropTarget::editor;
  } else if (repository_tree.contains(point)) {
    drop_target_ = DropTarget::repository_tree;
  } else {
    drop_target_ = DropTarget::none;
  }
  return drop_target_;
}

void InputState::clear_drop_target() { drop_target_ = DropTarget::none; }

auto InputState::drop_target() const -> DropTarget { return drop_target_; }

auto make_clipboard_payload(std::string_view text, std::string_view source_path)
    -> ClipboardPayload {
  auto output = ClipboardPayload{};
  output.plain_text = text;
  output.html = "<pre data-liquify-path=\"";
  output.html += escaped_html(source_path);
  output.html += "\">";
  output.html += escaped_html(text);
  output.html += "</pre>";
  return output;
}

auto html_to_plain_text(std::string_view html) -> std::string {
  auto output = std::string{};
  output.reserve(html.size());
  for (std::size_t index = 0; index < html.size();) {
    if (html[index] == '<') {
      const auto end = html.find('>', index + 1);
      if (end == std::string_view::npos) {
        break;
      }
      const auto name = tag_name(html.substr(index + 1, end - index - 1));
      if (name == "br" || name == "p" || name == "div") {
        append_line_break(output);
      }
      index = end + 1;
      continue;
    }
    if (html[index] == '&') {
      const auto end = html.find(';', index + 1);
      if (end != std::string_view::npos) {
        append_entity(output, html.substr(index + 1, end - index - 1));
        index = end + 1;
        continue;
      }
    }
    output.push_back(html[index]);
    ++index;
  }
  while (!output.empty() && output.back() == '\n') {
    output.pop_back();
  }
  return output;
}

auto preferred_clipboard_text(std::optional<std::string_view> plain_text,
                              std::optional<std::string_view> html)
    -> std::optional<std::string> {
  if (plain_text) {
    return std::string(*plain_text);
  }
  if (html) {
    return html_to_plain_text(*html);
  }
  return std::nullopt;
}

auto utf8_codepoint_count(std::string_view text) -> std::size_t {
  return static_cast<std::size_t>(
      std::ranges::count_if(text, [](unsigned char character) {
        return (character & 0xc0U) != 0x80U;
      }));
}

auto utf8_byte_offset(std::string_view text, std::size_t codepoint)
    -> std::size_t {
  auto current = std::size_t{};
  auto byte = std::size_t{};
  while (byte < text.size() && current < codepoint) {
    ++byte;
    while (byte < text.size() &&
           (static_cast<unsigned char>(text[byte]) & 0xc0U) == 0x80U) {
      ++byte;
    }
    ++current;
  }
  return byte;
}

auto transition_duration(float duration, AccessibilitySettings settings)
    -> float {
  return settings.reduced_motion ? 0.0F : std::max(duration, 0.0F);
}

auto accessibility_nodes() -> std::span<const AccessibilityNode> {
  return nodes;
}

} // namespace liquify
