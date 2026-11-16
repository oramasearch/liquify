// The one translation unit that knows SDL3 and Dear ImGui. The shell uses
// ImGui; the editor submits retained line geometry directly through
// SDL_Renderer with ImGui's GPU glyph atlas.
#include "liquify/app.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "misc/cpp/imgui_stdlib.h"
#include <SDL3/SDL.h>
#ifdef LIQUIFY_NATIVE_WINDOW_TESTS
#include "../tests/native_acceptance.hpp"
#include "../tests/ui_property_assert.hpp"
#include "imgui_internal.h"
#endif

#if defined(__APPLE__) && defined(__OBJC__)
#import <AppKit/AppKit.h>
#endif

#include "editor_buffer.hpp"
#include "liquid/hash.hpp"
#include "liquid/identity.hpp"
#include "liquid/liquid.hpp"
#include "liquify/ai_config.hpp"
#include "liquify/assistant.hpp"
#include "liquify/command.hpp"
#include "liquify/design_tokens.generated.hpp"
#include "liquify/editor_graph.hpp"
#include "liquify/multiplayer.hpp"
#include "liquify/screenshot.hpp"
#include "liquify/settings.hpp"
#include "liquify/stub_tree.hpp"
#include "liquify/version.hpp"
#include "repository_view.hpp"
#include "text_layout.hpp"

#ifdef LIQUIFY_NATIVE_BUFFER_REGRESSION
#include "../tests/native_buffer_regression.hpp"
#endif

#if defined(__APPLE__) && defined(__OBJC__)
enum NativeMenuCommand : std::int32_t {
  menu_save = 1,
  menu_close,
  menu_quit,
  menu_palette,
  menu_sidebar,
  menu_panel,
  menu_split,
  menu_theme,
  menu_contrast,
  menu_copy,
  menu_cut,
  menu_paste,
  menu_definition,
  menu_hover,
  menu_build,
  menu_tests,
  menu_push,
  menu_next_hunk,
  menu_previous_hunk,
  menu_next_comment,
  menu_expand_selection,
  menu_shrink_selection,
  menu_quick_open,
  menu_symbol_search,
  menu_references,
  menu_call_hierarchy,
  menu_open_recent,
  menu_back,
  menu_forward,
  menu_cut_change,
};

@interface LiquifyMenuTarget : NSObject
- (void)performLiquifyCommand:(id)sender;
@end

@implementation LiquifyMenuTarget
- (void)performLiquifyCommand:(id)sender {
  auto event = SDL_Event{};
  event.type = SDL_EVENT_USER;
  event.user.code = static_cast<Sint32>([sender tag]);
  SDL_PushEvent(&event);
}
@end

struct NativeMenuEntry {
  NSString *title;
  NSString *key;
  NSEventModifierFlags modifiers;
  NativeMenuCommand command;
};

static LiquifyMenuTarget *native_menu_target = nullptr;
static NSMutableArray *native_accessibility_elements = nullptr;

static void add_native_menu(NSMenu *main_menu, NSString *title,
                            std::initializer_list<NativeMenuEntry> entries) {
  auto *root =
      [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
  auto *menu = [[NSMenu alloc] initWithTitle:title];
  for (const auto &entry : entries) {
    auto *item =
        [[NSMenuItem alloc] initWithTitle:entry.title
                                   action:@selector(performLiquifyCommand:)
                            keyEquivalent:entry.key];
    [item setKeyEquivalentModifierMask:entry.modifiers];
    [item setTag:entry.command];
    [item setTarget:native_menu_target];
    [menu addItem:item];
  }
  [root setSubmenu:menu];
  [main_menu addItem:root];
}

static void install_native_menu() {
  native_menu_target = [[LiquifyMenuTarget alloc] init];
  auto *main_menu = [[NSMenu alloc] initWithTitle:@"liquify"];
  auto *application_root = [[NSMenuItem alloc] initWithTitle:@"liquify"
                                                      action:nil
                                               keyEquivalent:@""];
  auto *application_menu = [[NSMenu alloc] initWithTitle:@"liquify"];
  auto *about =
      [[NSMenuItem alloc] initWithTitle:@"About liquify"
                                 action:@selector(orderFrontStandardAboutPanel:)
                          keyEquivalent:@""];
  [application_menu addItem:about];
  [application_menu addItem:[NSMenuItem separatorItem]];
  auto *quit =
      [[NSMenuItem alloc] initWithTitle:@"Quit liquify"
                                 action:@selector(performLiquifyCommand:)
                          keyEquivalent:@"q"];
  [quit setTag:menu_quit];
  [quit setTarget:native_menu_target];
  [application_menu addItem:quit];
  [application_root setSubmenu:application_menu];
  [main_menu addItem:application_root];

  constexpr auto command = NSEventModifierFlagCommand;
  constexpr auto command_shift =
      NSEventModifierFlagCommand | NSEventModifierFlagShift;
  add_native_menu(main_menu, @"File",
                  {{@"Save", @"s", command, menu_save},
                   {@"Close File", @"w", command, menu_close}});
  add_native_menu(main_menu, @"Edit",
                  {{@"Copy", @"c", command, menu_copy},
                   {@"Cut", @"x", command, menu_cut},
                   {@"Paste", @"v", command, menu_paste},
                   {@"Command Palette", @"p", command_shift, menu_palette}});
  add_native_menu(main_menu, @"Selection",
                  {{@"Expand Selection", @"", 0, menu_expand_selection},
                   {@"Shrink Selection", @"", 0, menu_shrink_selection}});
  add_native_menu(
      main_menu, @"View",
      {{@"Toggle Sidebar", @"b", command, menu_sidebar},
       {@"Toggle Bottom Panel", @"j", command, menu_panel},
       {@"Toggle Editor Split", @"\\", command, menu_split},
       {@"Cycle Color Theme", @"t", command_shift, menu_theme},
       {@"Toggle High Contrast", @"h", command_shift, menu_contrast}});
  add_native_menu(main_menu, @"Go",
                  {{@"Quick Open", @"p", command, menu_quick_open},
                   {@"Symbol Search", @"t", command, menu_symbol_search},
                   {@"Go to Definition", @"", 0, menu_definition},
                   {@"Find References", @"", 0, menu_references},
                   {@"Call Hierarchy", @"", 0, menu_call_hierarchy},
                   {@"Open Recent", @"r", command, menu_open_recent},
                   {@"Back", @"", 0, menu_back},
                   {@"Forward", @"", 0, menu_forward},
                   {@"Show Hover", @"", 0, menu_hover}});
  add_native_menu(main_menu, @"Change",
                  {{@"Cut Change…", @"k", command_shift, menu_cut_change}});
  add_native_menu(main_menu, @"Run",
                  {{@"Build", @"", 0, menu_build},
                   {@"Test", @"", 0, menu_tests},
                   {@"Push", @"", 0, menu_push}});
  add_native_menu(main_menu, @"Review",
                  {{@"Next Hunk", @"", 0, menu_next_hunk},
                   {@"Previous Hunk", @"", 0, menu_previous_hunk},
                   {@"Next Comment", @"", 0, menu_next_comment}});
  [NSApp setMainMenu:main_menu];
}

static NSString *accessibility_role(liquify::AccessibilityRole role) {
  switch (role) {
  case liquify::AccessibilityRole::group:
    return NSAccessibilityGroupRole;
  case liquify::AccessibilityRole::button:
    return NSAccessibilityButtonRole;
  case liquify::AccessibilityRole::text_area:
    return NSAccessibilityTextAreaRole;
  }
  return NSAccessibilityGroupRole;
}

static void install_native_accessibility(SDL_Window *window) {
  const auto properties = SDL_GetWindowProperties(window);
  auto *native_window = static_cast<NSWindow *>(SDL_GetPointerProperty(
      properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
  if (native_window == nullptr || native_window.contentView == nullptr) {
    return;
  }
  auto *content = native_window.contentView;
  content.accessibilityElement = YES;
  content.accessibilityRole = NSAccessibilityGroupRole;
  content.accessibilityLabel = @"liquify editor";
  native_accessibility_elements = [[NSMutableArray alloc] init];
  auto y = content.bounds.size.height - 36.0;
  for (const auto &node : liquify::accessibility_nodes()) {
    auto *label = [[NSString alloc] initWithBytes:node.label.data()
                                           length:node.label.size()
                                         encoding:NSUTF8StringEncoding];
    auto *identifier = [[NSString alloc] initWithBytes:node.id.data()
                                                length:node.id.size()
                                              encoding:NSUTF8StringEncoding];
    auto *help = [[NSString alloc] initWithBytes:node.help.data()
                                          length:node.help.size()
                                        encoding:NSUTF8StringEncoding];
    NSAccessibilityElement *element = [NSAccessibilityElement
        accessibilityElementWithRole:accessibility_role(node.role)
                               frame:NSZeroRect
                               label:label
                              parent:content];
    element.accessibilityIdentifier = identifier;
    element.accessibilityHelp = help;
    element.accessibilityFrameInParentSpace =
        NSMakeRect(0.0, y, content.bounds.size.width, 32.0);
    [native_accessibility_elements addObject:element];
    y = std::max(y - 34.0, 0.0);
  }
  content.accessibilityChildren = native_accessibility_elements;
  content.accessibilityChildrenInNavigationOrder =
      (NSArray<id<NSAccessibilityElement>> *)native_accessibility_elements;
}

static bool system_high_contrast() {
  return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast;
}

static bool system_reduced_motion() {
  return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}

#endif

namespace liquify {
namespace {

constexpr float editor_padding = design::space2;
constexpr float editor_gutter_width = 72.0F;
constexpr float activity_bar_width = design::control_lg;
constexpr float divider_size = design::space1;

auto normalized_binding(std::string_view binding) -> std::string {
  auto output = std::string{};
  for (const auto value : binding) {
    if (value != ' ') {
      output.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
    }
  }
  return output;
}

auto binding_matches(std::string_view binding, std::uint32_t key,
                     std::uint16_t modifiers) -> bool {
  const auto normalized = normalized_binding(binding);
  const auto primary = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
  const auto shift = (modifiers & SDL_KMOD_SHIFT) != 0;
  const auto alt = (modifiers & SDL_KMOD_ALT) != 0;
  const auto wants_primary = normalized.contains("cmd+") ||
                             normalized.contains("ctrl+") ||
                             normalized.contains("primary+");
  const auto wants_shift = normalized.contains("shift+");
  const auto wants_alt =
      normalized.contains("alt+") || normalized.contains("option+");
  if (primary != wants_primary || shift != wants_shift || alt != wants_alt) {
    return false;
  }
  const auto separator = normalized.rfind('+');
  const auto expected_key = separator == std::string::npos
                                ? normalized
                                : normalized.substr(separator + 1);
  return expected_key == normalized_binding(SDL_GetKeyName(key));
}

auto command_matches(const Settings &settings, std::string_view command,
                     std::string_view default_binding, std::uint32_t key,
                     std::uint16_t modifiers) -> bool {
  const auto found = settings.keymap.find(std::string(command));
  const auto binding = found == settings.keymap.end()
                           ? default_binding
                           : std::string_view(found->second);
  return binding_matches(binding, key, modifiers);
}

auto imgui_color(design::Rgba color) -> ImVec4 {
  return {color.red, color.green, color.blue, color.alpha};
}

auto packed_color(design::Rgba color) -> ImU32 {
  return ImGui::ColorConvertFloat4ToU32(imgui_color(color));
}

// UI-only formatting: source, model values and clipboard payloads stay intact.
auto compact_identifier(std::string_view value) -> std::string {
  const auto colon = value.rfind(':');
  const auto payload = colon == std::string_view::npos ? 0 : colon + 1;
  if (value.size() - payload <= 8) {
    return std::string(value);
  }
  return std::string(value.substr(0, payload + 8)) + "…";
}

auto compact_identifiers(std::string_view text) -> std::string {
  auto output = std::string{};
  for (std::size_t offset = 0; offset < text.size();) {
    auto end = offset;
    while (end < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[end])) ||
            text[end] == ':')) {
      ++end;
    }
    if (end == offset) {
      output += text[offset++];
      continue;
    }
    const auto word = text.substr(offset, end - offset);
    const auto hex =
        word.size() >= 32 && std::ranges::all_of(word, [](unsigned char c) {
          return std::isxdigit(c);
        });
    output += word.starts_with("taap:") || word.starts_with("blake3:") || hex
                  ? compact_identifier(word)
                  : std::string(word);
    offset = end;
  }
  return output;
}

void identifier_detail(const char *label, const std::string &value) {
  if (value.empty()) {
    return;
  }
  ImGui::PushID(label);
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  const auto button = compact_identifier(value) + "##copy";
  if (ImGui::SmallButton(button.c_str())) {
    SDL_SetClipboardText(value.c_str());
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s\nClick to copy", value.c_str());
  }
  ImGui::PopID();
}

void detail_text(const std::string &value) {
  ImGui::TextWrapped("%s", compact_identifiers(value).c_str());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", value.c_str());
  }
}

enum class ShellIcon {
  folder,
  search,
  review,
  build,
  people,
  diagnostic,
  assistant,
  plugin,
  structure,
  document,
  code,
  header,
  markdown,
  settings,
  data
};

auto panel_icon(PanelId id) -> ShellIcon {
  switch (id) {
  case PanelId::repository:
    return ShellIcon::folder;
  case PanelId::search:
    return ShellIcon::search;
  case PanelId::review:
    return ShellIcon::review;
  case PanelId::build:
    return ShellIcon::build;
  case PanelId::multiplayer:
    return ShellIcon::people;
  case PanelId::diagnostics:
    return ShellIcon::diagnostic;
  case PanelId::assistant:
    return ShellIcon::assistant;
  case PanelId::plugins:
    return ShellIcon::plugin;
  case PanelId::structural_editing:
    return ShellIcon::structure;
  }
  return ShellIcon::document;
}

auto file_icon(NavigationFileKind kind) -> ShellIcon {
  switch (kind) {
  case NavigationFileKind::directory:
    return ShellIcon::folder;
  case NavigationFileKind::c_source:
  case NavigationFileKind::cpp_source:
    return ShellIcon::code;
  case NavigationFileKind::header:
    return ShellIcon::header;
  case NavigationFileKind::markdown:
    return ShellIcon::markdown;
  case NavigationFileKind::cmake:
    return ShellIcon::settings;
  case NavigationFileKind::json:
  case NavigationFileKind::binary:
    return ShellIcon::data;
  default:
    return ShellIcon::document;
  }
}

// Small native strokes avoid relying on icon glyphs absent from IBM Plex.
void draw_shell_icon(ShellIcon icon, ImVec2 origin) {
  auto *draw = ImGui::GetWindowDrawList();
  const auto color = ImGui::GetColorU32(ImGuiCol_Text);
  const auto point = [origin](float x, float y) {
    return ImVec2(origin.x + x, origin.y + y);
  };
  const auto line = [&](float x, float y, float u, float v) {
    draw->AddLine(point(x, y), point(u, v), color, 1.5F);
  };
  switch (icon) {
  case ShellIcon::folder:
    draw->AddRect(point(1, 5), point(15, 14), color, 1.5F, 0, 1.5F);
    line(1, 5, 1, 2);
    line(1, 2, 7, 2);
    line(7, 2, 9, 5);
    break;
  case ShellIcon::search:
    draw->AddCircle(point(6, 6), 4.5F, color, 16, 1.5F);
    line(10, 10, 15, 15);
    break;
  case ShellIcon::build:
    draw->AddTriangle(point(3, 1), point(14, 8), point(3, 15), color, 1.5F);
    break;
  case ShellIcon::people:
    draw->AddCircle(point(5, 4), 2.5F, color, 12, 1.5F);
    draw->AddCircle(point(12, 5), 2.0F, color, 12, 1.5F);
    draw->AddRect(point(1, 9), point(9, 15), color, 3.0F, 0, 1.5F);
    line(11, 10, 15, 10);
    line(15, 10, 15, 15);
    break;
  case ShellIcon::diagnostic:
    draw->AddTriangle(point(8, 1), point(15, 15), point(1, 15), color, 1.5F);
    line(8, 6, 8, 10);
    draw->AddCircleFilled(point(8, 12.5F), 1, color);
    break;
  case ShellIcon::assistant:
    line(8, 1, 8, 15);
    line(1, 8, 15, 8);
    line(3, 3, 13, 13);
    line(3, 13, 13, 3);
    break;
  case ShellIcon::plugin:
  case ShellIcon::structure:
  case ShellIcon::settings:
    draw->AddRect(point(3, 4), point(13, 13), color, 1, 0, 1.5F);
    line(5, 0, 5, 4);
    line(11, 0, 11, 4);
    line(8, 13, 8, 16);
    break;
  case ShellIcon::review:
    draw->AddRect(point(1, 2), point(15, 12), color, 2, 0, 1.5F);
    line(4, 12, 4, 16);
    line(4, 16, 8, 12);
    break;
  default:
    draw->AddRect(point(2, 1), point(14, 15), color, 1, 0, 1.5F);
    if (icon == ShellIcon::code || icon == ShellIcon::header) {
      line(6, 6, 4, 8);
      line(4, 8, 6, 10);
      line(10, 6, 12, 8);
      line(12, 8, 10, 10);
      if (icon == ShellIcon::header) {
        line(5, 3, 11, 3);
      }
    } else {
      line(5, 5, 11, 5);
      line(5, 8, 11, 8);
      line(5, 11, icon == ShellIcon::markdown ? 8 : 11, 11);
    }
    break;
  }
}

constexpr float icon_box = 16.0F;
constexpr float icon_inset = design::space1;

void item_icon(ShellIcon icon, float inset = icon_inset) {
  const auto begin = ImGui::GetItemRectMin();
  const auto height = ImGui::GetItemRectSize().y;
  draw_shell_icon(icon,
                  {begin.x + inset, begin.y + (height - icon_box) * 0.5F});
}

// Widgets that own their hit area — Selectable, TreeNode, TabItem — take one
// label, so the room for the icon has to live inside that string. Deriving the
// count from the font's own advance keeps the gap a real gap at any font size
// or display scale; a fixed number of spaces silently closes up the moment
// either changes, which is what put icons against their labels.
auto icon_label(std::string_view text, float text_offset,
                float inset = icon_inset) -> std::string {
  const auto reserved = inset + icon_box + design::space2 - text_offset;
  const auto advance = ImGui::CalcTextSize(" ").x;
  const auto count =
      advance > 0.0F && reserved > 0.0F
          ? static_cast<std::size_t>(std::ceil(reserved / advance))
          : std::size_t{0};
  return std::string(count, ' ') + std::string(text);
}

void file_type_hint(NavigationFileKind kind) {
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", navigation_file_kind_name(kind).data());
  }
}

bool file_item(const char *name, NavigationFileKind kind,
               bool selected = false) {
  // Selectable draws its label at the item's left edge; it applies frame
  // padding vertically only, so the reservation starts from zero here.
  const auto label = icon_label(name, 0.0F);
  const auto clicked = ImGui::Selectable(label.c_str(), selected);
  item_icon(file_icon(kind));
  file_type_hint(kind);
  return clicked;
}

bool directory_item(const char *name) {
  const auto spacing = ImGui::GetTreeNodeToLabelSpacing();
  const auto label = icon_label(name, spacing, spacing);
  const auto open = ImGui::TreeNode(label.c_str());
  item_icon(ShellIcon::folder, ImGui::GetTreeNodeToLabelSpacing());
  file_type_hint(NavigationFileKind::directory);
  return open;
}

auto effective_palette(bool dark, bool high_contrast) -> design::Palette {
  auto palette = dark ? design::dark_palette : design::light_palette;
  if (!high_contrast) {
    return palette;
  }
  palette.canvas =
      dark ? design::dark_palette.canvas : design::light_palette.surface;
  palette.surface = palette.canvas;
  palette.layer =
      dark ? design::dark_palette.surface : design::light_palette.canvas;
  palette.layer_hover = palette.selected;
  palette.border_subtle = palette.text_primary;
  palette.border_strong = palette.text_primary;
  palette.border_interactive = palette.focus;
  palette.text_disabled = palette.text_primary;
  palette.text_secondary = palette.text_primary;
  palette.text_tertiary = palette.text_primary;
  return palette;
}

#if !defined(__APPLE__) || !defined(__OBJC__)
auto system_high_contrast() -> bool {
  return false;
}

auto system_reduced_motion() -> bool {
  return false;
}
#endif

constexpr auto clipboard_plain_utf8 = "text/plain;charset=utf-8";
constexpr auto clipboard_plain = "text/plain";
constexpr auto clipboard_html = "text/html";

const void *SDLCALL clipboard_data(void *userdata, const char *mime_type,
                                   std::size_t *size) {
  const auto *payload = static_cast<const ClipboardPayload *>(userdata);
  const auto type = std::string_view{mime_type == nullptr ? "" : mime_type};
  const auto *value =
      type == clipboard_html ? &payload->html : &payload->plain_text;
  *size = value->size();
  return value->data();
}

void SDLCALL clipboard_cleanup(void *userdata) {
  delete static_cast<ClipboardPayload *>(userdata);
}

auto clipboard_value(std::string_view mime_type) -> std::optional<std::string> {
  auto size = std::size_t{};
  auto *data = SDL_GetClipboardData(std::string(mime_type).c_str(), &size);
  if (data == nullptr) {
    return std::nullopt;
  }
  auto value = std::string(static_cast<const char *>(data), size);
  SDL_free(data);
  return value;
}

auto cjk_font_path() -> std::filesystem::path {
#if defined(__APPLE__)
  return "/System/Library/Fonts/PingFang.ttc";
#elif defined(_WIN32)
  return "C:/Windows/Fonts/msyh.ttc";
#else
  for (const auto *path :
       {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"}) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }
  return {};
#endif
}

auto smoke_text() -> std::string {
  constexpr std::size_t line_count = 50'000;
  std::string text;
  text.reserve(line_count * 52);
  for (std::size_t line = 0; line < line_count; ++line) {
    text += "line ";
    text += std::to_string(line + 1);
    text += "  retained GPU text layer smoke content";
    if (line + 1 < line_count) {
      text.push_back('\n');
    }
  }
  return text;
}

auto screenshot_text() -> std::string {
  return R"cpp(#include <print>
#include <string_view>

namespace liquify {

auto greeting(std::string_view repository) -> std::string {
    return "Open " + std::string(repository) + " through libliquid";
}

} // namespace liquify

int main() {
    std::println("{}", liquify::greeting("orama/liquify"));
    return 0;
}
)cpp";
}

auto syntax_color(SyntaxTokenKind kind, const design::Palette &palette)
    -> design::Rgba {
  switch (kind) {
  case SyntaxTokenKind::keyword:
    return palette.action;
  case SyntaxTokenKind::type:
    return palette.ai;
  case SyntaxTokenKind::function:
    return palette.info;
  case SyntaxTokenKind::string:
    return palette.success;
  case SyntaxTokenKind::number:
    return palette.warning;
  case SyntaxTokenKind::comment:
    return palette.text_tertiary;
  case SyntaxTokenKind::preprocessor:
    return palette.danger;
  case SyntaxTokenKind::punctuation:
    return palette.text_secondary;
  case SyntaxTokenKind::plain:
  case SyntaxTokenKind::variable:
    return palette.text_primary;
  }
  return palette.text_primary;
}

auto screenshot_syntax(std::string_view source) -> std::vector<SyntaxSpan> {
  auto output = std::vector<SyntaxSpan>{};
  const auto append = [&](std::string_view token, SyntaxTokenKind kind) {
    auto offset = std::size_t{};
    while ((offset = source.find(token, offset)) != std::string_view::npos) {
      output.push_back({offset, offset + token.size(), kind});
      offset += token.size();
    }
  };
  append("#include", SyntaxTokenKind::preprocessor);
  append("namespace", SyntaxTokenKind::keyword);
  append("auto", SyntaxTokenKind::keyword);
  append("return", SyntaxTokenKind::keyword);
  append("int", SyntaxTokenKind::type);
  append("std::string", SyntaxTokenKind::type);
  append("std::string_view", SyntaxTokenKind::type);
  append("greeting", SyntaxTokenKind::function);
  append("main", SyntaxTokenKind::function);
  append("\"Open \"", SyntaxTokenKind::string);
  append("\"orama/liquify\"", SyntaxTokenKind::string);
  append("// namespace liquify", SyntaxTokenKind::comment);
  std::ranges::sort(output, {}, &SyntaxSpan::start_byte);
  return output;
}

auto syntax_frontend_path() -> std::filesystem::path {
  const auto configured = std::filesystem::path{LIQUIFY_TREE_SITTER_FRONTEND};
  const auto *base = SDL_GetBasePath();
  if (base != nullptr) {
    const auto adjacent =
        std::filesystem::path{base} / "frontends" / configured.filename();
    if (std::filesystem::exists(adjacent)) {
      return adjacent;
    }
  }
  return configured;
}

auto percentile(std::vector<double> values, double fraction) -> double {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(
      std::ceil(fraction * static_cast<double>(values.size())) - 1.0);
  return values[std::min(index, values.size() - 1)];
}

auto average(const std::vector<double> &values) -> double {
  if (values.empty()) {
    return 0.0;
  }
  double total = 0.0;
  for (const auto value : values) {
    total += value;
  }
  return total / static_cast<double>(values.size());
}

auto source_edit_between(std::string_view before, std::string_view after)
    -> std::optional<SourceEdit> {
  if (before == after) {
    return std::nullopt;
  }
  auto first = std::size_t{};
  while (first < before.size() && first < after.size() &&
         before[first] == after[first]) {
    ++first;
  }
  auto before_end = before.size();
  auto after_end = after.size();
  while (before_end > first && after_end > first &&
         before[before_end - 1] == after[after_end - 1]) {
    --before_end;
    --after_end;
  }
  if (first > std::numeric_limits<std::uint32_t>::max() ||
      before_end > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return SourceEdit{static_cast<std::uint32_t>(first),
                    static_cast<std::uint32_t>(before_end),
                    std::string(after.substr(first, after_end - first))};
}

auto build_action_state_name(BuildActionState state) -> std::string_view {
  switch (state) {
  case BuildActionState::queued:
    return "queued";
  case BuildActionState::running_local:
    return "running local";
  case BuildActionState::running_remote:
    return "running remote";
  case BuildActionState::hit:
    return "hit";
  case BuildActionState::done:
    return "done";
  case BuildActionState::failed:
    return "failed";
  }
  return "unknown";
}

auto push_outcome_name(liquid::sync_ref_outcome_kind outcome)
    -> std::string_view {
  switch (outcome) {
  case liquid::sync_ref_outcome_kind::applied:
    return "applied";
  case liquid::sync_ref_outcome_kind::refused:
    return "refused";
  }
  return "unknown";
}

// A table's header row in lake's style: muted ink on the surface role, with a hairline under
// it. Records the row's rectangle for the contrast property test.
void table_headers(const design::Palette &palette, UiFrameLayout &layout,
                   std::string_view id) {
  ImGui::PushStyleColor(ImGuiCol_Text, imgui_color(palette.text_secondary));
  ImGui::TableHeadersRow();
  ImGui::PopStyleColor();
  const auto top_left = ImGui::GetItemRectMin();
  const auto bottom = ImGui::GetItemRectMax().y;
  auto *table = ImGui::GetCurrentTable();
  const auto left = table != nullptr ? table->OuterRect.Min.x : top_left.x;
  const auto right = table != nullptr ? table->OuterRect.Max.x : ImGui::GetItemRectMax().x;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(left, bottom), ImVec2(right, bottom),
                                      packed_color(palette.border_subtle), 1.0F);
  layout.controls[std::string(id) + "/header"] =
      InputRectangle{left, top_left.y, right - left, bottom - top_left.y};
}

auto graph_query_failure(const EditorGraphSnapshot &snapshot) -> liquid::error {
  return snapshot.query_failure.value_or(
      liquid::error{liquid::errc::internal, snapshot.query_error, ""});
}

auto pixel_color(ImVec4 color) -> UiPixelColor {
  return {static_cast<std::uint8_t>(std::round(color.x * 255.0F)),
          static_cast<std::uint8_t>(std::round(color.y * 255.0F)),
          static_cast<std::uint8_t>(std::round(color.z * 255.0F)),
          static_cast<std::uint8_t>(std::round(color.w * 255.0F))};
}

auto item_rectangle() -> InputRectangle {
  const auto start = ImGui::GetItemRectMin();
  const auto size = ImGui::GetItemRectSize();
  return {start.x, start.y, size.x, size.y};
}

// Call around text-only drawing; backgrounds and icons must not enter the ink
// bounds.
auto text_geometry(int first_vertex) -> UiTextGeometry {
  const auto &vertices = ImGui::GetWindowDrawList()->VtxBuffer;
  if (first_vertex == vertices.Size) {
    return {};
  }
  auto minimum = vertices[first_vertex].pos;
  auto maximum = minimum;
  for (int index = first_vertex; index < vertices.Size; ++index) {
    const auto position = vertices[index].pos;
    minimum.x = std::min(minimum.x, position.x);
    minimum.y = std::min(minimum.y, position.y);
    maximum.x = std::max(maximum.x, position.x);
    maximum.y = std::max(maximum.y, position.y);
  }
  return {{minimum.x, minimum.y, maximum.x - minimum.x, maximum.y - minimum.y},
          ImGui::GetTextLineHeight()};
}

} // namespace

class RetainedTextLayer {
public:
  void reset(std::size_t line_count) {
    lines_.clear();
    lines_.resize(line_count);
  }

  void apply_change(BufferChange change) {
    const auto first =
        lines_.begin() + static_cast<std::ptrdiff_t>(change.first_line);
    const auto last = first + static_cast<std::ptrdiff_t>(change.erased_lines);
    const auto insertion = lines_.erase(first, last);
    lines_.insert(insertion, change.inserted_lines, std::nullopt);
  }

  auto
  render(SDL_Renderer *renderer, EditorBuffer &buffer, TextLayoutCache &layouts,
         ImFont &font, const design::Palette &palette,
         std::span<const Diagnostic> diagnostics,
         std::span<const SyntaxSpan> syntax, std::span<const FoldRange> folds,
         std::span<const AttributionSnapshot> attribution,
         std::span<const PresenceSnapshot> presence,
         const std::optional<liquid::drive_path_status> &drive,
         std::uint64_t syntax_generation, float x, float y, float width,
         float height, std::size_t first_line, float line_height,
         float cell_width, float horizontal_scroll,
         std::span<const ReviewDiffLine> review_lines,
         std::span<const std::size_t> comment_lines, bool show_whitespace)
      -> std::size_t {
    const auto color_changed =
        text_color_.red != palette.text_primary.red ||
        text_color_.green != palette.text_primary.green ||
        text_color_.blue != palette.text_primary.blue ||
        text_color_.alpha != palette.text_primary.alpha;
    if (cell_width_ != cell_width || font_size_ != font.FontSize ||
        color_changed || syntax_generation_ != syntax_generation) {
      cell_width_ = cell_width;
      font_size_ = font.FontSize;
      text_color_ = palette.text_primary;
      syntax_generation_ = syntax_generation;
      for (auto &line : lines_) {
        line.reset();
      }
    }

    const auto available_lines =
        static_cast<std::size_t>(std::ceil(height / line_height)) + 1;
    const auto last_line =
        std::min(buffer.line_count(), first_line + available_lines);
    draw_gutter(renderer, buffer, diagnostics, folds, attribution, presence,
                drive, palette, x, y, first_line, last_line, line_height);
    draw_review_backgrounds(renderer, review_lines, x, y, width, first_line,
                            last_line, line_height);
    draw_selection(renderer, buffer, layouts, x, y, width, first_line,
                   last_line, line_height, cell_width, horizontal_scroll);
    draw_bracket_match(renderer, buffer, layouts, palette, x, y, first_line,
                       last_line, line_height, cell_width, horizontal_scroll);

    auto *texture =
        reinterpret_cast<SDL_Texture *>(ImGui::GetIO().Fonts->TexID);
    frame_vertices_.clear();
    frame_indices_.clear();
    for (std::size_t line = first_line; line < last_line; ++line) {
      const auto &layout = layouts.line(line, buffer.line(line));
      auto &retained = lines_.at(line);
      if (!retained || retained->generation != layout.generation) {
        retained =
            build_geometry(layout, font, cell_width,
                           buffer.byte_offset({line, 0}), syntax, palette);
      }
      append_line(*retained,
                  x + editor_gutter_width + editor_padding - horizontal_scroll,
                  y + static_cast<float>(line - first_line) * line_height);
    }
    if (!frame_vertices_.empty()) {
      SDL_RenderGeometry(renderer, texture, frame_vertices_.data(),
                         static_cast<int>(frame_vertices_.size()),
                         frame_indices_.data(),
                         static_cast<int>(frame_indices_.size()));
    }

    if (show_whitespace) {
      draw_whitespace(renderer, buffer, layouts, palette, x, y, first_line,
                      last_line, line_height, cell_width, horizontal_scroll);
    }
    draw_diagnostics(renderer, buffer, diagnostics, x, y, first_line, last_line,
                     line_height, cell_width, horizontal_scroll);
    draw_review_comments(renderer, comment_lines, x, y, first_line, last_line,
                         line_height);
    draw_scroll_markers(renderer, buffer, diagnostics, attribution, presence,
                        palette, x + width - 5.0F, y, height);
    draw_cursor(renderer, buffer, layouts, x, y, first_line, last_line,
                line_height, cell_width, horizontal_scroll);
    return last_line - first_line;
  }

  auto visible_text(InputRectangle clip, float line_height) const
      -> UiTextGeometry {
    auto result = UiTextGeometry{};
    for (std::size_t index = 0; index + 3 < frame_vertices_.size();
         index += 4) {
      const auto &first = frame_vertices_[index].position;
      const auto &last = frame_vertices_[index + 2].position;
      if (last.x <= first.x || last.y <= first.y || first.x < clip.x ||
          first.y < clip.y || last.x > clip.x + clip.width ||
          last.y > clip.y + clip.height) {
        continue;
      }
      if (result.ink.width == 0) {
        result.ink = {first.x, first.y, last.x - first.x, last.y - first.y};
        continue;
      }
      const auto right = std::max(result.ink.x + result.ink.width, last.x);
      const auto bottom = std::max(result.ink.y + result.ink.height, last.y);
      result.ink.x = std::min(result.ink.x, first.x);
      result.ink.y = std::min(result.ink.y, first.y);
      result.ink.width = right - result.ink.x;
      result.ink.height = bottom - result.ink.y;
    }
    result.line_height = line_height;
    return result;
  }

private:
  struct LineGeometry {
    std::uint64_t generation{};
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
  };

  static auto build_geometry(const LineLayout &layout, ImFont &font,
                             float cell_width, std::size_t line_offset,
                             std::span<const SyntaxSpan> syntax,
                             const design::Palette &palette) -> LineGeometry {
    LineGeometry geometry;
    geometry.generation = layout.generation;
    geometry.vertices.reserve(layout.glyphs.size() * 4);
    geometry.indices.reserve(layout.glyphs.size() * 6);
    for (const auto &placed : layout.glyphs) {
      if (placed.codepoint == '\t') {
        continue;
      }
      const auto *glyph = font.FindGlyph(placed.codepoint);
      if (glyph == nullptr || !glyph->Visible) {
        continue;
      }
      const auto first = static_cast<int>(geometry.vertices.size());
      const auto origin = placed.column * cell_width;
      const auto absolute_byte = line_offset + placed.byte;
      const auto found =
          std::ranges::find_if(syntax, [absolute_byte](const auto &span) {
            return absolute_byte >= span.start_byte &&
                   absolute_byte < span.end_byte;
          });
      const auto token =
          found == syntax.end() ? SyntaxTokenKind::plain : found->kind;
      const auto role = syntax_color(token, palette);
      const SDL_FColor color{role.red, role.green, role.blue, role.alpha};
      geometry.vertices.push_back(
          {{origin + glyph->X0, glyph->Y0}, color, {glyph->U0, glyph->V0}});
      geometry.vertices.push_back(
          {{origin + glyph->X1, glyph->Y0}, color, {glyph->U1, glyph->V0}});
      geometry.vertices.push_back(
          {{origin + glyph->X1, glyph->Y1}, color, {glyph->U1, glyph->V1}});
      geometry.vertices.push_back(
          {{origin + glyph->X0, glyph->Y1}, color, {glyph->U0, glyph->V1}});
      geometry.indices.insert(
          geometry.indices.end(),
          {first, first + 1, first + 2, first, first + 2, first + 3});
    }
    return geometry;
  }

  static void draw_gutter(SDL_Renderer *renderer, EditorBuffer &buffer,
                          std::span<const Diagnostic> diagnostics,
                          std::span<const FoldRange> folds,
                          std::span<const AttributionSnapshot> attribution,
                          std::span<const PresenceSnapshot> presence,
                          const std::optional<liquid::drive_path_status> &drive,
                          const design::Palette &palette, float x, float y,
                          std::size_t first_line, std::size_t last_line,
                          float line_height) {
    SDL_SetRenderDrawColorFloat(renderer, palette.layer.red,
                                palette.layer.green, palette.layer.blue,
                                palette.layer.alpha);
    const SDL_FRect background{x, y, editor_gutter_width,
                               static_cast<float>(last_line - first_line) *
                                   line_height};
    SDL_RenderFillRect(renderer, &background);
    SDL_SetRenderDrawColorFloat(
        renderer, palette.border_subtle.red, palette.border_subtle.green,
        palette.border_subtle.blue, palette.border_subtle.alpha);
    SDL_RenderLine(renderer, x + editor_gutter_width - 1.0F, y,
                   x + editor_gutter_width - 1.0F, y + background.h);
    SDL_SetRenderDrawColorFloat(
        renderer, palette.text_tertiary.red, palette.text_tertiary.green,
        palette.text_tertiary.blue, palette.text_tertiary.alpha);
    for (auto line = first_line; line < last_line; ++line) {
      SDL_RenderDebugText(
          renderer, x + 30.0F,
          y + static_cast<float>(line - first_line) * line_height + 3.0F,
          std::to_string(line + 1).c_str());
    }
    for (const auto &item : attribution) {
      if (item.line < first_line || item.line >= last_line) {
        continue;
      }
      const auto color = item.agent ? palette.ai : palette.info;
      SDL_SetRenderDrawColorFloat(renderer, color.red, color.green, color.blue,
                                  color.alpha);
      const SDL_FRect marker{
          x + 2.0F,
          y + static_cast<float>(item.line - first_line) * line_height + 2.0F,
          3.0F, line_height - 4.0F};
      SDL_RenderFillRect(renderer, &marker);
    }
    for (const auto &item : presence) {
      if (item.path != buffer.display_path() || !item.range ||
          item.range->start_line < first_line ||
          item.range->start_line >= last_line) {
        continue;
      }
      SDL_SetRenderDrawColorFloat(renderer, palette.ai.red, palette.ai.green,
                                  palette.ai.blue, palette.ai.alpha);
      const auto row =
          static_cast<float>(item.range->start_line - first_line) * line_height;
      const SDL_FRect marker{x + 8.0F, y + row + 5.0F, 6.0F, 6.0F};
      SDL_RenderFillRect(renderer, &marker);
    }
    for (const auto &diagnostic : diagnostics) {
      if (diagnostic.location.path.generic_string() != buffer.display_path()) {
        continue;
      }
      const auto line =
          static_cast<std::size_t>(diagnostic.location.range.start_line);
      if (line < first_line || line >= last_line) {
        continue;
      }
      const auto color =
          diagnostic.severity == DiagnosticSeverity::error     ? palette.danger
          : diagnostic.severity == DiagnosticSeverity::warning ? palette.warning
                                                               : palette.info;
      SDL_SetRenderDrawColorFloat(renderer, color.red, color.green, color.blue,
                                  color.alpha);
      const SDL_FRect marker{
          x + 18.0F,
          y + static_cast<float>(line - first_line) * line_height + 6.0F, 6.0F,
          6.0F};
      SDL_RenderFillRect(renderer, &marker);
    }
    for (const auto &fold : folds) {
      if (fold.first_line < first_line || fold.first_line >= last_line) {
        continue;
      }
      SDL_SetRenderDrawColorFloat(
          renderer, palette.text_secondary.red, palette.text_secondary.green,
          palette.text_secondary.blue, palette.text_secondary.alpha);
      const auto row =
          y + static_cast<float>(fold.first_line - first_line) * line_height;
      const SDL_FPoint points[]{{x + 58.0F, row + 6.0F},
                                {x + 64.0F, row + 9.0F},
                                {x + 58.0F, row + 12.0F}};
      SDL_RenderLines(renderer, points, 3);
    }
    if (drive) {
      SDL_SetRenderDrawColorFloat(renderer, palette.success.red,
                                  palette.success.green, palette.success.blue,
                                  palette.success.alpha);
      const SDL_FRect badge{x + 7.0F, y + 2.0F, 5.0F, 5.0F};
      SDL_RenderFillRect(renderer, &badge);
    }
  }

  static void
  draw_scroll_markers(SDL_Renderer *renderer, EditorBuffer &buffer,
                      std::span<const Diagnostic> diagnostics,
                      std::span<const AttributionSnapshot> attribution,
                      std::span<const PresenceSnapshot> presence,
                      const design::Palette &palette, float x, float y,
                      float height) {
    const auto marker = [&](std::size_t line, design::Rgba color) {
      const auto fraction =
          static_cast<float>(line) /
          static_cast<float>(std::max<std::size_t>(buffer.line_count(), 1));
      SDL_SetRenderDrawColorFloat(renderer, color.red, color.green, color.blue,
                                  color.alpha);
      const SDL_FRect rectangle{x, y + fraction * height, 4.0F, 3.0F};
      SDL_RenderFillRect(renderer, &rectangle);
    };
    for (const auto &diagnostic : diagnostics) {
      if (diagnostic.location.path.generic_string() == buffer.display_path()) {
        marker(diagnostic.location.range.start_line,
               diagnostic.severity == DiagnosticSeverity::error
                   ? palette.danger
                   : palette.warning);
      }
    }
    for (const auto &item : attribution) {
      marker(item.line, item.agent ? palette.ai : palette.info);
    }
    for (const auto &item : presence) {
      if (item.path == buffer.display_path() && item.range) {
        marker(item.range->start_line, palette.ai);
      }
    }
  }

  void append_line(const LineGeometry &geometry, float x, float y) {
    const auto first = static_cast<int>(frame_vertices_.size());
    for (const auto &retained : geometry.vertices) {
      auto vertex = retained;
      vertex.position.x += x;
      vertex.position.y += y;
      frame_vertices_.push_back(vertex);
    }
    for (const auto index : geometry.indices) {
      frame_indices_.push_back(first + index);
    }
  }

  static void draw_selection(SDL_Renderer *renderer, EditorBuffer &buffer,
                             TextLayoutCache &layouts, float x, float y,
                             float width, std::size_t first_line,
                             std::size_t last_line, float line_height,
                             float cell_width, float horizontal_scroll) {
    SDL_SetRenderDrawColor(renderer, 176, 207, 255, 255);
    for (const auto &selection : buffer.selections()) {
      const auto begin_line = std::max(first_line, selection.begin.line);
      const auto end_line = std::min(last_line, selection.end.line + 1);
      for (std::size_t line = begin_line; line < end_line; ++line) {
        const auto &layout = layouts.line(line, buffer.line(line));
        const auto first_byte =
            line == selection.begin.line ? selection.begin.byte : 0;
        const auto last_byte = line == selection.end.line
                                   ? selection.end.byte
                                   : buffer.line(line).size();
        const auto first_column = layout.column_for_byte(first_byte);
        auto last_column = layout.column_for_byte(last_byte);
        if (line < selection.end.line) {
          last_column += 1.0F;
        }
        const SDL_FRect rectangle{
            x + editor_gutter_width + editor_padding +
                first_column * cell_width - horizontal_scroll,
            y + static_cast<float>(line - first_line) * line_height,
            std::min((last_column - first_column) * cell_width, width),
            line_height};
        SDL_RenderFillRect(renderer, &rectangle);
      }
    }
  }

  static void draw_cursor(SDL_Renderer *renderer, EditorBuffer &buffer,
                          TextLayoutCache &layouts, float x, float y,
                          std::size_t first_line, std::size_t last_line,
                          float line_height, float cell_width,
                          float horizontal_scroll) {
    SDL_SetRenderDrawColor(renderer, 28, 35, 43, 255);
    for (const auto &cursor : buffer.cursors()) {
      if (cursor.line < first_line || cursor.line >= last_line) {
        continue;
      }
      const auto &layout = layouts.line(cursor.line, buffer.line(cursor.line));
      const auto column = layout.column_for_byte(cursor.byte);
      const auto width =
          buffer.mode() == EditingMode::modal_command ? cell_width : 1.5F;
      const SDL_FRect rectangle{
          x + editor_gutter_width + editor_padding + column * cell_width -
              horizontal_scroll,
          y + static_cast<float>(cursor.line - first_line) * line_height, width,
          line_height};
      SDL_RenderFillRect(renderer, &rectangle);
    }
  }

  static void draw_whitespace(SDL_Renderer *renderer, EditorBuffer &buffer,
                              TextLayoutCache &layouts,
                              const design::Palette &palette, float x, float y,
                              std::size_t first_line, std::size_t last_line,
                              float line_height, float cell_width,
                              float horizontal_scroll) {
    SDL_SetRenderDrawColorFloat(
        renderer, palette.text_disabled.red, palette.text_disabled.green,
        palette.text_disabled.blue, palette.text_disabled.alpha);
    for (auto line = first_line; line < last_line; ++line) {
      const auto text = buffer.line(line);
      const auto &layout = layouts.line(line, text);
      for (std::size_t byte = 0; byte < text.size(); ++byte) {
        if (text[byte] != ' ' && text[byte] != '\t') {
          continue;
        }
        const auto column = layout.column_for_byte(byte);
        const auto marker_x = x + editor_gutter_width + editor_padding +
                              (column + 0.5F) * cell_width - horizontal_scroll;
        const auto marker_y =
            y + static_cast<float>(line - first_line) * line_height +
            line_height * 0.55F;
        if (text[byte] == ' ') {
          SDL_RenderPoint(renderer, marker_x, marker_y);
        } else {
          SDL_RenderLine(renderer, marker_x - 2.0F, marker_y, marker_x + 2.0F,
                         marker_y);
        }
      }
    }
  }

  static void draw_bracket_match(SDL_Renderer *renderer, EditorBuffer &buffer,
                                 TextLayoutCache &layouts,
                                 const design::Palette &palette, float x,
                                 float y, std::size_t first_line,
                                 std::size_t last_line, float line_height,
                                 float cell_width, float horizontal_scroll) {
    const auto source = buffer.text();
    const auto cursor_offset = buffer.byte_offset(buffer.cursor());
    const auto matched = matching_bracket(source, cursor_offset);
    if (!matched) {
      return;
    }
    auto bracket = cursor_offset;
    if (bracket >= source.size() ||
        std::string_view{"()[]{}"}.find(source[bracket]) ==
            std::string_view::npos) {
      bracket = bracket == 0 ? 0 : bracket - 1;
    }
    SDL_SetRenderDrawColorFloat(renderer, palette.focus.red,
                                palette.focus.green, palette.focus.blue,
                                palette.focus.alpha);
    for (const auto offset : {bracket, *matched}) {
      auto line = std::size_t{};
      while (line + 1 < buffer.line_count() &&
             buffer.byte_offset({line + 1, 0}) <= offset) {
        ++line;
      }
      if (line < first_line || line >= last_line) {
        continue;
      }
      const auto byte = offset - buffer.byte_offset({line, 0});
      const auto column =
          layouts.line(line, buffer.line(line)).column_for_byte(byte);
      const SDL_FRect rectangle{x + editor_gutter_width + editor_padding +
                                    column * cell_width - horizontal_scroll,
                                y + static_cast<float>(line - first_line) *
                                        line_height,
                                cell_width, line_height};
      SDL_RenderRect(renderer, &rectangle);
    }
  }

  static void
  draw_review_backgrounds(SDL_Renderer *renderer,
                          std::span<const ReviewDiffLine> review_lines, float x,
                          float y, float width, std::size_t first_line,
                          std::size_t last_line, float line_height) {
    for (auto line = first_line; line < last_line && line < review_lines.size();
         ++line) {
      const auto kind = review_lines[line].kind;
      if (kind == ReviewDiffLineKind::added) {
        SDL_SetRenderDrawColor(renderer, 224, 246, 226, 255);
      } else if (kind == ReviewDiffLineKind::removed) {
        SDL_SetRenderDrawColor(renderer, 252, 228, 228, 255);
      } else if (kind == ReviewDiffLineKind::conflict) {
        SDL_SetRenderDrawColor(renderer, 255, 235, 196, 255);
      } else if (kind == ReviewDiffLineKind::file ||
                 kind == ReviewDiffLineKind::hunk) {
        SDL_SetRenderDrawColor(renderer, 228, 235, 245, 255);
      } else {
        continue;
      }
      const SDL_FRect rectangle{
          x, y + static_cast<float>(line - first_line) * line_height, width,
          line_height};
      SDL_RenderFillRect(renderer, &rectangle);
    }
  }

  static void draw_review_comments(SDL_Renderer *renderer,
                                   std::span<const std::size_t> comment_lines,
                                   float x, float y, std::size_t first_line,
                                   std::size_t last_line, float line_height) {
    SDL_SetRenderDrawColor(renderer, 99, 74, 176, 255);
    for (const auto line : comment_lines) {
      if (line < first_line || line >= last_line) {
        continue;
      }
      const SDL_FRect marker{
          x + 1.0F,
          y + static_cast<float>(line - first_line) * line_height + 2.0F, 4.0F,
          std::max(2.0F, line_height - 4.0F)};
      SDL_RenderFillRect(renderer, &marker);
    }
  }

  static void draw_diagnostics(SDL_Renderer *renderer, EditorBuffer &buffer,
                               std::span<const Diagnostic> diagnostics, float x,
                               float y, std::size_t first_line,
                               std::size_t last_line, float line_height,
                               float cell_width, float horizontal_scroll) {
    for (const auto &diagnostic : diagnostics) {
      if (diagnostic.location.path.generic_string() != buffer.display_path()) {
        continue;
      }
      const auto &range = diagnostic.location.range;
      const auto begin_line =
          std::max(first_line, static_cast<std::size_t>(range.start_line));
      const auto end_line =
          std::min(last_line, static_cast<std::size_t>(range.end_line) + 1);
      if (diagnostic.severity == DiagnosticSeverity::error) {
        SDL_SetRenderDrawColor(renderer, 205, 46, 46, 255);
      } else if (diagnostic.severity == DiagnosticSeverity::warning) {
        SDL_SetRenderDrawColor(renderer, 204, 128, 0, 255);
      } else {
        SDL_SetRenderDrawColor(renderer, 52, 101, 164, 255);
      }
      for (auto line = begin_line; line < end_line; ++line) {
        const auto first_column =
            line == range.start_line ? range.start_column : 0U;
        auto last_column =
            line == range.end_line
                ? range.end_column
                : static_cast<std::uint32_t>(buffer.line(line).size());
        last_column = std::max(last_column, first_column + 1);
        const auto start_x = x + editor_gutter_width + editor_padding +
                             static_cast<float>(first_column) * cell_width -
                             horizontal_scroll;
        const auto finish_x = x + editor_gutter_width + editor_padding +
                              static_cast<float>(last_column) * cell_width -
                              horizontal_scroll;
        const auto baseline =
            y + static_cast<float>(line - first_line + 1) * line_height - 2.0F;
        auto upward = true;
        for (auto position = start_x; position < finish_x; position += 4.0F) {
          const auto next = std::min(position + 4.0F, finish_x);
          SDL_RenderLine(renderer, position, baseline + (upward ? 1.0F : -1.0F),
                         next, baseline + (upward ? -1.0F : 1.0F));
          upward = !upward;
        }
      }
    }
  }

  std::vector<std::optional<LineGeometry>> lines_;
  std::vector<SDL_Vertex> frame_vertices_;
  std::vector<int> frame_indices_;
  float cell_width_{};
  float font_size_{};
  design::Rgba text_color_{};
  std::uint64_t syntax_generation_{};
};

App::App(AppOptions options) : options_(std::move(options)) {
  multiplayer_session_ = make_multiplayer_session();
  const auto *requested_screenshot =
      options_.screenshot_state
          ? find_screenshot_state(*options_.screenshot_state)
          : nullptr;
  if (requested_screenshot == nullptr) {
    auto loaded_settings = load_settings(options_.repository_path);
    settings_ = std::move(loaded_settings.settings);
    settings_diagnostics_ = std::move(loaded_settings.diagnostics);
  }
  settings_scope_ = options_.repository_path ? SettingsScope::repository
                                             : SettingsScope::user;
  shell_state_.theme = settings_.theme;
  first_run_state_path_ = default_first_run_state_path();
  if (!options_.screenshot_state) {
    auto loaded = load_first_run_state(first_run_state_path_);
    first_run_ = std::move(loaded.state);
    if (!loaded.error.empty()) {
      editor_status_ = std::move(loaded.error);
    }
  }
  const auto initial_repository = options_.repository_path;
  options_.repository_path.reset();
  if (initial_repository) {
    auto repository = std::make_unique<RepositoryView>(
        RepositoryView::open(*initial_repository));
    if (repository->error().empty()) {
      if (activate_repository(std::move(repository))) {
        first_run_.repository_opened(repository_summary());
        persist_first_run();
      } else if (repository_) {
        first_run_.repository_opened(repository_summary());
      }
    } else {
      first_run_.open_failed(*initial_repository,
                             std::string(repository->error()));
    }
  } else {
    panels_.restore(shell_state_.panel_layout);
  }
  if (options_.smoke) {
    transient_editor_ = std::make_unique<EditorBuffer>(EditorBuffer::from_text(
        "smoke-50000", "smoke-50000.txt", smoke_text()));
    editor_ = transient_editor_.get();
    layouts_ = std::make_unique<TextLayoutCache>();
    layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
    text_layer_ = std::make_unique<RetainedTextLayer>();
    text_layer_->reset(editor_->line_count());
    editor_focused_ = true;
  }
  const auto *screenshot =
      options_.screenshot_state
          ? find_screenshot_state(*options_.screenshot_state)
          : nullptr;
  if (screenshot != nullptr) {
    options_.forced_theme = screenshot->theme == ScreenshotTheme::dark
                                ? ThemePreference::dark
                                : ThemePreference::light;
    shell_state_.high_contrast = screenshot->high_contrast;
    first_run_.restore(
        {{.path = "/Users/ora/projects/liquify",
          .name = "liquify",
          .last_change = "First-run repository lifecycle",
          .last_changed_at = "Today",
          .branch = "issue-36-first-run",
          .opened_at = std::chrono::sys_seconds{std::chrono::seconds{30}}},
         {.path = "/Users/ora/projects/liquid",
          .name = "liquid",
          .last_change = "Drive clone support",
          .last_changed_at = "Yesterday",
          .branch = "main",
          .opened_at = std::chrono::sys_seconds{std::chrono::seconds{20}}},
         {.path = "/Users/ora/projects/orama-bridge",
          .name = "orama-bridge",
          .last_change = "Update tracking",
          .last_changed_at = "Sep 4",
          .branch = "main",
          .opened_at = std::chrono::sys_seconds{std::chrono::seconds{10}}}},
        IdentitySession{.actor = "taap:1:ed25519:7YQ2M4AB",
                        .custody = "local device"});
    if (screenshot->view == ScreenshotView::repository_unreadable) {
      first_run_.open_failed("/Users/ora/projects/missing",
                             "repository marker is not readable");
    } else if (screenshot->view == ScreenshotView::lake_offline) {
      first_run_.begin_clone("https://lake.orama.com/orama/example",
                             "/Users/ora/projects/example");
      first_run_.clone_failed(describe_clone_failure(
          {liquid::errc::unavailable, "nodename nor servname provided, or not known",
           "lake.orama.com"},
          "https://lake.orama.com/orama/example"));
    }
  }
  if (screenshot != nullptr && screenshot->view != ScreenshotView::welcome &&
      screenshot->view != ScreenshotView::repository_unreadable &&
      screenshot->view != ScreenshotView::lake_offline) {
    transient_editor_ = std::make_unique<EditorBuffer>(EditorBuffer::from_text(
        "screenshot-main", "src/app.cpp", screenshot_text()));
    editor_ = transient_editor_.get();
    layouts_ = std::make_unique<TextLayoutCache>();
    layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
    text_layer_ = std::make_unique<RetainedTextLayer>();
    text_layer_->reset(editor_->line_count());
    editor_focused_ = true;
    if (screenshot->view == ScreenshotView::accessibility) {
      editor_->set_cursor({5, 9});
      input_.update_composition("日本語", 2, 1);
    }
    if (screenshot->view == ScreenshotView::review) {
      panels_.focus(PanelId::review);
    } else if (screenshot->view == ScreenshotView::editor_view) {
      panels_.focus(PanelId::search);
    } else if (screenshot->view == ScreenshotView::accessibility) {
      panels_.focus(PanelId::repository);
    } else {
      panels_.focus(PanelId::repository);
    }
    shell_state_.bottom_panel_visible =
        screenshot->view == ScreenshotView::diagnostics ||
        screenshot->view == ScreenshotView::build ||
        screenshot->view == ScreenshotView::assistant ||
        screenshot->view == ScreenshotView::plugins ||
        screenshot->view == ScreenshotView::editor_view ||
        screenshot->view == ScreenshotView::panels;
    shell_state_.editor_split = screenshot->view == ScreenshotView::editor_cpp;
    if (screenshot->view == ScreenshotView::build) {
      panels_.focus(PanelId::build);
    } else if (screenshot->view == ScreenshotView::assistant) {
      panels_.focus(PanelId::assistant);
    } else if (screenshot->view == ScreenshotView::plugins) {
      panels_.focus(PanelId::plugins);
    } else if (screenshot->view == ScreenshotView::editor_view) {
      sidebar_search_ = "greeting";
      find_query_ = "repository";
      find_open_ = true;
      settings_.editor.wrap = true;
      editor_->set_preferences({.indent_style = IndentStyle::spaces,
                                .indent_size = 4,
                                .soft_wrap = true});
      editor_->set_cursor({5, 5});
      editor_->add_cursor({9, 4});
      buffer_matches_ = editor_->find(find_query_);
    } else {
      panels_.focus(PanelId::diagnostics);
    }
    if (screenshot->view == ScreenshotView::panels) {
      panels_.move(PanelId::build, PanelPlacement::editor_tab);
      panels_.move(PanelId::assistant, PanelPlacement::floating);
      panels_.update_floating_rect(PanelId::assistant,
                                   {760.0F, 110.0F, 460.0F, 280.0F});
      panels_.focus(PanelId::build);
      panels_.begin_operation("build", PanelId::build, "Building repository");
      panels_.update_operation("build", "Building repository", 2, 4);
    } else {
      panels_.clear_focus_request();
    }
    if (screenshot->view == ScreenshotView::navigation) {
      quick_open_query_ = "app";
      quick_open_open_ = true;
    }
    if (screenshot->view == ScreenshotView::command_palette) {
      command_palette_open_ = true;
    }
    if (screenshot->view == ScreenshotView::settings) {
      settings_open_ = true;
      settings_.theme = screenshot->theme == ScreenshotTheme::dark
                            ? ThemePreference::dark
                            : ThemePreference::light;
      settings_.editor.wrap = true;
      settings_.keymap["build.run"] = "F7";
      settings_.lake_accounts.push_back(
          {"orama", "https://api.oramasearch.com/liquid"});
    }
  }
}

bool App::activate_repository(std::unique_ptr<RepositoryView> repository) {
  if (!confirm_discard("Switch repository")) {
    return false;
  }
  if (repository_) {
    persist_shell_state();
  }
  if (repository_operation_is_clone_) {
    options_.lake_url = pending_lake_url_;
  }
  editor_ = nullptr;
  open_buffers_.clear();
  transient_editor_.reset();
  analysis_buffer_open_ = false;
  analysis_text_.clear();
  navigation_history_ = NavigationHistory{};
  layouts_.reset();
  text_layer_.reset();
  editor_graph_.reset();
  review_.reset();
  assistant_.reset();
  plugin_host_.reset();
  plugin_failures_.clear();
  plugin_untrusted_.clear();
  plugin_panels_.clear();
  multiplayer_.reset();
  multiplayer_refresh_ = {};
  next_multiplayer_refresh_ = {};
  find_query_.clear();
  replace_query_.clear();
  buffer_matches_.clear();
  find_match_index_ = 0;
  column_selection_anchor_.reset();
  repository_ = std::move(repository);
  options_.repository_path = repository_->path();
  if (!screenshot_mode()) {
    auto loaded_settings = load_settings(options_.repository_path);
    settings_ = std::move(loaded_settings.settings);
    settings_diagnostics_ = std::move(loaded_settings.diagnostics);
  }
  settings_scope_ = SettingsScope::repository;

  auto loaded_state = load_app_shell_state(*options_.repository_path);
  shell_state_ = loaded_state.state;
  shell_state_.theme = settings_.theme;
  if (!loaded_state.error.empty()) {
    editor_status_ = std::move(loaded_state.error);
  } else {
    editor_status_.clear();
  }
  panels_ = PanelRegistry{};
  panels_.restore(shell_state_.panel_layout);

  auto plugins = load_configured_plugins(*options_.repository_path);
  if (!plugins) {
    plugin_host_ = std::make_shared<PluginHost>();
    auto message = plugins.error().message;
    if (!plugins.error().context.empty()) {
      message += ": " + plugins.error().context;
    }
    plugin_failures_.push_back({{}, std::move(message)});
  } else {
    plugin_host_ = std::move(plugins->host);
    plugin_failures_ = std::move(plugins->failures);
    plugin_untrusted_ = std::move(plugins->untrusted);
  }
  editor_graph_ = std::make_unique<EditorGraph>(
      *options_.repository_path, plugin_host_, syntax_frontend_path());

  auto config = AiConfig{.enabled = settings_.ai.enabled,
                         .context_bytes = settings_.ai.context_bytes,
                         .model = settings_.ai.model,
                         .max_tokens = settings_.ai.max_tokens};
  ai_context_bytes_ = config.context_bytes;
  auto key = ConfiguredSecret{};
  if (config.enabled) {
    key = anthropic_api_key();
  }
  const auto secret = key.value;
  assistant_ = std::make_unique<Assistant>(config, key.configured, [secret] {
    return std::make_unique<AnthropicProvider>(secret);
  });
  // Reviews live on the repository's lake: --lake when given, else the lake the repository
  // was cloned from or that Set Lake recorded.
  if (!options_.lake_url || lake_url_from_repository_) {
    options_.lake_url.reset();
    lake_url_from_repository_ = false;
    // Read once with the repository at open: a second store handle opened here, while the
    // tree and index load, is what liquify#97 / liquid#526 corrupt.
    if (!repository_->lake_remote().empty()) {
      options_.lake_url = std::string(repository_->lake_remote());
      lake_url_from_repository_ = true;
    }
  }
  review_.reset();
  configure_review();
  configure_commands();
  return true;
}

void App::configure_review() {
  if (!options_.lake_url || !options_.repository_path || review_) {
    return;
  }
  try {
    review_ = std::make_unique<ReviewController>(
        *options_.repository_path, *options_.lake_url, lake_token());
    review_->request_list();
  } catch (const std::exception &error) {
    editor_status_ = std::string{"review configuration: "} + error.what();
  }
}

void App::open_request_review() {
  if (!repository_) {
    editor_status_ = "Open a repository to request a review";
    return;
  }
  if (!review_) {
    open_set_lake_dialog();
    return;
  }
  request_review_requested_ = true;
  request_review_reviewers_.clear();
  panels_.focus(PanelId::review);
  shell_state_.sidebar_visible = true;
}

void App::draw_request_review_dialog() {
  if (request_review_requested_) {
    ImGui::OpenPopup("Request Review");
    request_review_requested_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(560.0F, 0.0F), ImGuiCond_Appearing);
  if (!begin_dialog("Request Review", ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::PushTextWrapPos(540.0F);
  ImGui::TextUnformatted(
      "Asks people on lake to read your current change before it lands. Push "
      "the change first, so lake has it.");
  ImGui::PopTextWrapPos();
  if (repository_ && !repository_->current_change().empty()) {
    ImGui::TextDisabled(
        "Current change %s",
        compact_identifiers(std::string(repository_->current_change())).c_str());
  }
  ImGui::Spacing();
  ImGui::TextUnformatted("Reviewers (optional)");
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(540.0F);
  ImGui::InputTextWithHint("##reviewers", "Names, separated by commas",
                           &request_review_reviewers_);
  frame_layout_.controls["request-review/reviewers"] = item_rectangle();
  ImGui::Spacing();
  if (kit_button("Request review", ButtonKind::primary)) {
    review_->request_review(parse_reviewers(request_review_reviewers_));
    editor_status_ = "Requesting a review";
    ImGui::CloseCurrentPopup();
  }
  frame_layout_.controls["request-review/submit"] = item_rectangle();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

auto App::repository_summary() const -> RepositorySummary {
  if (!repository_) {
    return {};
  }
  return {
      .path = repository_->path(),
      .name = repository_->name(),
      .last_change = repository_->last_change().empty()
                         ? std::string{"No named change"}
                         : std::string{repository_->last_change()},
      .last_changed_at = std::string{repository_->last_changed_at()},
      .branch = repository_->branch().empty()
                    ? std::string{"No branch"}
                    : std::string{repository_->branch()},
      .current_change = std::string{repository_->current_change()},
      .offline_reason = std::string{repository_->offline_reason()},
      .remote_bytes = repository_->remote_bytes(),
      .hydration_debt = repository_->hydration_debt(),
      .pending_uploads = repository_->pending_uploads(),
      .presence_count = repository_->presence_count(),
  };
}

void App::persist_first_run() {
  if (screenshot_mode()) {
    return;
  }
  const auto error = save_first_run_state(first_run_state_path_, first_run_);
  if (!error.empty()) {
    editor_status_ = error;
  }
}

void App::begin_open_repository(std::filesystem::path path) {
  if (repository_operation_) {
    return;
  }
  first_run_.begin_open(path);
  repository_operation_is_clone_ = false;
  request_repaint();
  repository_operation_.emplace(
      std::async(std::launch::async, [path = std::move(path)] {
        return std::make_unique<RepositoryView>(RepositoryView::open(path));
      }));
}

void App::begin_clone_repository(std::string remote,
                                 std::filesystem::path path) {
  if (repository_operation_) {
    return;
  }
  first_run_.begin_clone(remote, path);
  pending_lake_url_ = remote;
  repository_operation_is_clone_ = true;
  const auto *configured_token = std::getenv("LIQUID_TOKEN");
  const auto token = configured_token == nullptr
                         ? std::string{}
                         : std::string{configured_token};
  request_repaint();
  repository_operation_.emplace(std::async(
      std::launch::async,
      [path = std::move(path), remote = std::move(remote), token]() mutable {
        return std::make_unique<RepositoryView>(
            RepositoryView::clone(std::move(path), std::move(remote), token));
      }));
}

void App::poll_repository_lifecycle() {
  if (!repository_operation_ ||
      repository_operation_->wait_for(std::chrono::milliseconds::zero()) !=
          std::future_status::ready) {
    return;
  }
  try {
    auto repository = repository_operation_->get();
    if (!repository->error().empty()) {
      if (repository_operation_is_clone_) {
        auto failure = repository->clone_failure();
        if (failure.empty()) {
          failure = {.summary = "The clone did not finish",
                     .next_step = "Retry the clone.",
                     .details = without_absolute_paths(repository->error())};
        }
        first_run_.clone_failed(std::move(failure));
      } else {
        first_run_.open_failed(first_run_.pending_path(),
                               std::string{repository->error()});
      }
    } else {
      if (activate_repository(std::move(repository))) {
        first_run_.repository_opened(repository_summary());
        persist_first_run();
      } else if (repository_) {
        first_run_.repository_opened(repository_summary());
      }
    }
  } catch (const std::exception &error) {
    if (repository_operation_is_clone_) {
      first_run_.clone_failed({.summary = "The clone did not finish",
                               .next_step = "Retry the clone.",
                               .details = without_absolute_paths(error.what())});
    } else {
      first_run_.open_failed(first_run_.pending_path(), error.what());
    }
  }
  repository_operation_.reset();
  pending_lake_url_.reset();
}

void App::show_folder_dialog() {
  if (folder_dialog_open_) {
    return;
  }
  folder_dialog_open_ = true;
  const auto default_location =
      repository_ ? repository_->path().string() : std::string{};
  SDL_ShowOpenFolderDialog(
      [](void *userdata, const char *const *files, int) {
        auto *app = static_cast<App *>(userdata);
        app->folder_dialog_open_ = false;
        if (files != nullptr && files[0] != nullptr) {
          app->begin_open_repository(files[0]);
        }
      },
      this, window_,
      default_location.empty() ? nullptr : default_location.c_str(), false);
}

void App::open_new_window(std::optional<std::filesystem::path> repository) {
  const auto executable = options_.executable_path.string();
  if (executable.empty()) {
    editor_status_ = "cannot locate the liquify executable";
    return;
  }
  const auto repository_argument =
      repository ? repository->string() : std::string{};
  const char *arguments[] = {
      executable.c_str(),
      repository ? repository_argument.c_str() : nullptr,
      nullptr,
  };
  auto *process = SDL_CreateProcess(arguments, false);
  if (process == nullptr) {
    editor_status_ =
        std::string{"cannot open another window: "} + SDL_GetError();
    return;
  }
  SDL_DestroyProcess(process);
}

void App::begin_sign_in() {
  if (identity_operation_) {
    return;
  }
  identity_error_.clear();
  request_repaint();
  identity_operation_.emplace(std::async(std::launch::async, [] {
    auto signer =
        liquid::runtime::block_on(liquid::identity_detail::local_signer());
    if (!signer) {
      auto message = signer.error().message;
      if (!signer.error().context.empty()) {
        message = signer.error().context + ": " + message;
      }
      throw std::runtime_error(message);
    }
    const auto actor = signer->identity();
    return IdentitySession{.actor = std::string{actor.identifier()},
                           .custody = actor.custody};
  }));
}

void App::poll_sign_in() {
  if (!identity_operation_ ||
      identity_operation_->wait_for(std::chrono::milliseconds::zero()) !=
          std::future_status::ready) {
    return;
  }
  try {
    first_run_.sign_in(identity_operation_->get());
    persist_first_run();
  } catch (const std::exception &error) {
    identity_error_ = error.what();
  }
  identity_operation_.reset();
}

App::~App() {
  shutdown();
}

void App::configure_commands() {
  commands_ = std::make_unique<CommandRegistry>();
  command_diagnostics_.clear();
  for (const auto &definition : builtin_commands()) {
    auto added = commands_->add(definition);
    if (!added) {
      command_diagnostics_.push_back(added.error());
    }
  }
  for (const auto &panel : standard_panel_descriptors()) {
    for (const auto &command : panel.commands) {
      if (commands_->find(command.id) != nullptr) {
        continue;
      }
      auto added = commands_->add({std::string(command.id),
                                   std::string(command.title),
                                   std::string(panel.title),
                                   "Command provided by a panel.",
                                   {}});
      if (!added) {
        command_diagnostics_.push_back(added.error());
      }
    }
  }
  if (plugin_host_) {
    for (const auto &plugin : plugin_host_->commands()) {
      auto bindings = std::vector<std::string>{};
      if (!plugin.default_binding.empty()) {
        bindings.push_back(plugin.default_binding);
      }
      auto added = commands_->add(
          {plugin.name, plugin.name, "Plugins",
           "Command registered by the " + plugin.plugin + " plugin.",
           std::move(bindings), true, true});
      if (!added) {
        command_diagnostics_.push_back(added.error());
      }
    }
  }
  const auto apply_file = [this](const std::filesystem::path &path) {
    const auto loaded = load_keymap(path);
    for (const auto &diagnostic : loaded.diagnostics) {
      command_diagnostics_.push_back(format_command_diagnostic(diagnostic));
    }
    for (const auto &diagnostic :
         commands_->apply_overrides(loaded.overrides)) {
      command_diagnostics_.push_back(format_command_diagnostic(diagnostic));
    }
  };
  if (!screenshot_mode() && !options_.smoke) {
    apply_file(global_settings_path());
    if (options_.repository_path) {
      apply_file(repository_settings_path(*options_.repository_path));
    }
  }
}

bool App::invoke_command(std::string_view name) {
  const auto *definition = commands_ ? commands_->find(name) : nullptr;
  if (definition == nullptr) {
    return false;
  }
  commands_->record_use(name);
  if (definition->plugin) {
    invoke_plugin_command(name);
    return true;
  }
  if (name == "file.save") {
    save_editor();
  } else if (name == "file.close") {
    close_editor();
  } else if (name == "edit.undo" || name == "edit.redo") {
    undo_editor(name == "edit.redo");
  } else if (name == "edit.command-palette") {
    command_palette_open_ = true;
  } else if (name == "settings.open") {
    settings_open_ = true;
  } else if (name == "view.toggle-sidebar") {
    shell_state_.sidebar_visible = !shell_state_.sidebar_visible;
    persist_shell_state();
  } else if (name == "view.toggle-bottom-panel") {
    shell_state_.bottom_panel_visible = !shell_state_.bottom_panel_visible;
    persist_shell_state();
  } else if (name == "view.toggle-editor-split") {
    shell_state_.editor_split = !shell_state_.editor_split;
    persist_shell_state();
  } else if (name == "view.cycle-theme") {
    cycle_theme();
  } else if (name == "go.definition") {
    request_definition();
  } else if (name == "go.hover") {
    request_hover();
  } else if (name == "go.completion") {
    request_completion();
  } else if (name == "run.build") {
    request_build();
  } else if (name == "run.tests") {
    request_tests();
  } else if (name == "run.push") {
    request_push();
  } else if (name == "change.cut") {
    open_cut_composer();
  } else if (name == "run.set-lake") {
    open_set_lake_dialog();
  } else if (name == "review.request") {
    open_request_review();
  } else if (name == "run.set-up-build") {
    open_build_setup();
  } else if (name == "assistant.toggle") {
    toggle_assistant();
  } else if (name == "panel.reset-layout") {
    reset_panel_layout();
  } else if (name == "help.version") {
    editor_status_ = "liquify " + std::string(version_string());
  } else {
    execute_panel_command(name);
  }
  return true;
}

bool App::init() {
  return init_window(true);
}

auto App::diagnostics_viewport() const -> InputRectangle {
  const auto found = frame_layout_.panels.find(PanelId::diagnostics);
  return found == frame_layout_.panels.end() ? InputRectangle{}
                                             : found->second.content;
}

auto App::frame_layout() const -> const UiFrameLayout & {
  return frame_layout_;
}

bool App::init_window(bool high_pixel_density) {
  const auto *screenshot =
      options_.screenshot_state
          ? find_screenshot_state(*options_.screenshot_state)
          : nullptr;
  if (options_.screenshot_state && screenshot == nullptr) {
    std::fprintf(stderr, "liquify: unknown screenshot state: %s\n",
                 options_.screenshot_state->c_str());
    return false;
  }
  if (options_.screenshot_state.has_value() !=
      options_.screenshot_output.has_value()) {
    std::fprintf(stderr,
                 "liquify: screenshot state and output must be set together\n");
    return false;
  }
  if (screenshot != nullptr) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "liquify: SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  int window_width = static_cast<int>(screenshot_width);
  int window_height = static_cast<int>(screenshot_height);
  SDL_Rect usable{};
  if (screenshot == nullptr &&
      SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) &&
      usable.w > 0 && usable.h > 0) {
    window_width = static_cast<int>(usable.w * 0.70F);
    window_height = static_cast<int>(usable.h * 0.70F);
    window_width = std::max(window_width, 900);
    window_height = std::max(window_height, 600);
  }
  const std::string title =
      options_.title + " " + std::string(version_string());
  const auto window_flags =
      screenshot == nullptr
          ? SDL_WINDOW_RESIZABLE |
                (high_pixel_density ? SDL_WINDOW_HIGH_PIXEL_DENSITY : 0)
          : SDL_WINDOW_HIDDEN;
  window_ = SDL_CreateWindow(title.c_str(), window_width, window_height,
                             window_flags);
  if (window_ == nullptr) {
    std::fprintf(stderr, "liquify: SDL_CreateWindow failed: %s\n",
                 SDL_GetError());
    SDL_Quit();
    return false;
  }
  renderer_ =
      SDL_CreateRenderer(window_, screenshot == nullptr ? nullptr : "software");
  if (renderer_ == nullptr) {
    std::fprintf(stderr, "liquify: SDL_CreateRenderer failed: %s\n",
                 SDL_GetError());
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
    return false;
  }
  SDL_SetRenderVSync(renderer_, screenshot == nullptr ? 1 : 0);
  if (screenshot == nullptr) {
    if (const auto *mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())) {
      display_refresh_hz_ = mode->refresh_rate;
    }
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  display_scale_ = screenshot == nullptr
                       ? std::max(SDL_GetWindowPixelDensity(window_), 1.0F)
                       : 1.0F;
  if (screenshot != nullptr) {
    io.DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
  }
  load_font_atlas(display_scale_);
  reduced_motion_ = system_reduced_motion();
  apply_theme();
  if (editor_) {
    auto preferences = editor_->preferences();
    preferences.soft_wrap = settings_.editor.wrap;
    editor_->set_preferences(preferences);
  }
  ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
  ImGui_ImplSDLRenderer3_Init(renderer_);
  initialized_ = true;
  if (!upload_font_atlas()) {
    return false;
  }
  SDL_StartTextInput(window_);
  configure_commands();
  if (screenshot != nullptr &&
      screenshot->view == ScreenshotView::command_palette) {
    commands_->record_use("view.repository");
  }
#if defined(__APPLE__) && defined(__OBJC__)
  if (!screenshot_mode()) {
    install_native_menu();
    install_native_accessibility(window_);
  }
#endif
  initialized_ = true;
  return true;
}

bool App::screenshot_mode() const {
  return options_.screenshot_state.has_value();
}

bool App::dark_theme_active() const {
  const auto preference = options_.forced_theme.value_or(shell_state_.theme);
  if (preference == ThemePreference::dark) {
    return true;
  }
  if (preference == ThemePreference::light) {
    return false;
  }
  return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
}

bool App::high_contrast_active() const {
  return shell_state_.high_contrast || system_high_contrast();
}

void App::load_font_atlas(float scale) {
  auto &io = ImGui::GetIO();
  io.Fonts->Clear();
  auto add_font = [&](const auto &bytes, float size, std::string_view name) {
    auto config = ImFontConfig{};
    config.FontDataOwnedByAtlas = false;
    config.RasterizerDensity = scale;
    std::snprintf(config.Name, sizeof(config.Name), "%.*s %.0f",
                  static_cast<int>(name.size()), name.data(), size);
    return io.Fonts->AddFontFromMemoryTTF(
        const_cast<std::uint8_t *>(bytes.data()),
        static_cast<int>(bytes.size()), size, &config);
  };
  ui_font_index_ = -1;
  editor_font_index_ = -1;
  heading_font_index_ = -1;
  auto ui_distance = std::numeric_limits<float>::max();
  auto editor_distance = std::numeric_limits<float>::max();
  for (const auto size :
       {design::text11, design::text12, design::text14, design::text16,
        design::text20, design::text24, design::text32}) {
    auto *font = add_font(design::ibm_plex_sans_regular, size, "IBM Plex Sans");
    const auto distance =
        std::abs(size - static_cast<float>(settings_.fonts.ui_size));
    if (distance < ui_distance) {
      ui_distance = distance;
      ui_font_index_ = io.Fonts->Fonts.Size - 1;
      io.FontDefault = font;
    }
    if (size == design::text24) {
      heading_font_index_ = io.Fonts->Fonts.Size - 1;
    }
  }
  // The type scale's emphasis faces: titles and headings are IBM Plex Sans SemiBold, as lake's.
  title_font_index_ = -1;
  section_font_index_ = -1;
  add_font(design::ibm_plex_sans_semibold, design::text24, "IBM Plex Sans SemiBold");
  title_font_index_ = io.Fonts->Fonts.Size - 1;
  add_font(design::ibm_plex_sans_semibold, design::text16, "IBM Plex Sans SemiBold");
  section_font_index_ = io.Fonts->Fonts.Size - 1;
  for (const auto size : {design::text12, design::text14, design::text16}) {
    add_font(design::ibm_plex_mono_regular, size, "IBM Plex Mono");
    const auto distance =
        std::abs(size - static_cast<float>(settings_.fonts.editor_size));
    if (distance < editor_distance) {
      editor_distance = distance;
      editor_font_index_ = io.Fonts->Fonts.Size - 1;
    }
  }
  io.FontDefault = io.Fonts->Fonts[ui_font_index_];
  // Atlas repacking changes UVs even when logical glyph metrics stay the same.
  if (text_layer_ && editor_) {
    text_layer_->reset(editor_->line_count());
  }
}

bool App::fail_rendering(std::string message) {
  rendering_failed_ = true;
  quit_ = true;
  std::fprintf(stderr, "liquify: %s\n", message.c_str());
  if (!screenshot_mode()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "liquify rendering error",
                             message.c_str(), window_);
  }
  return false;
}

bool App::upload_font_atlas() {
  auto &atlas = *ImGui::GetIO().Fonts;
  SDL_ClearError();
  const auto created =
      atlas.Build() && ImGui_ImplSDLRenderer3_CreateFontsTexture();
  auto *texture = reinterpret_cast<SDL_Texture *>(atlas.TexID);
  // The pinned backend checks allocation but ignores upload/configuration
  // results.
  const auto uploaded =
      created && texture != nullptr &&
      SDL_UpdateTexture(texture, nullptr, atlas.TexPixelsRGBA32,
                        atlas.TexWidth * 4) &&
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) &&
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
  if (uploaded) {
    return true;
  }
  const auto reason = std::string(SDL_GetError());
  const auto message =
      "Could not upload the font atlas (" + std::to_string(atlas.TexWidth) +
      " x " + std::to_string(atlas.TexHeight) + ") to the " +
      SDL_GetRendererName(renderer_) + " renderer. " +
      (reason.empty() ? "Font rasterization or texture creation failed."
                      : reason) +
      " Reduce UI/editor font sizes in settings.liquid and restart liquify. "
      "If this persists, include this message and display scale in a bug "
      "report.";
  ImGui_ImplSDLRenderer3_DestroyFontsTexture();
  return fail_rendering(message);
}

void App::apply_theme() {
  const auto dark = dark_theme_active();
  const auto high_contrast = high_contrast_active();
  const auto palette = effective_palette(dark, high_contrast);
  auto &style = ImGui::GetStyle();
  style = ImGuiStyle{};
  style.WindowPadding = {design::space3, design::space3};
  style.FramePadding = {design::space3, design::space2};
  style.CellPadding = {design::space2, design::space1};
  style.ItemSpacing = {design::space2, design::space2};
  style.ItemInnerSpacing = {design::space1, design::space1};
  style.ScrollbarSize = design::space3;
  style.GrabMinSize = design::space2;
  style.WindowRounding = design::panel_radius;
  style.ChildRounding = design::panel_radius;
  style.PopupRounding = design::overlay_radius;
  style.FrameRounding = design::control_radius;
  style.GrabRounding = design::control_radius;
  style.TabRounding = design::control_radius;
  const auto border_width = high_contrast ? 2.0F : design::border_width;
  style.WindowBorderSize = border_width;
  style.ChildBorderSize = border_width;
  style.PopupBorderSize = border_width;
  // Controls follow lake's kit (liquify#101): fields, checkboxes and secondary buttons carry a
  // hairline; the one primary action per surface is filled with the action role.
  style.FrameBorderSize = border_width;
  style.GrabMinSize = design::space4;
  style.TabBorderSize = high_contrast ? border_width : 0.0F;

  auto *colors = style.Colors;
  colors[ImGuiCol_Text] = imgui_color(palette.text_primary);
  colors[ImGuiCol_TextDisabled] = imgui_color(palette.text_disabled);
  colors[ImGuiCol_WindowBg] = imgui_color(palette.canvas);
  colors[ImGuiCol_ChildBg] = imgui_color(palette.surface);
  colors[ImGuiCol_PopupBg] = imgui_color(palette.overlay);
  colors[ImGuiCol_Border] = imgui_color(palette.border_subtle);
  // Compact tables with sticky headers, as lake's kit draws them: the header sits on the
  // surface role, its labels in the muted ink role, with a hairline under it (liquify#109).
  colors[ImGuiCol_TableHeaderBg] = imgui_color(palette.surface);
  colors[ImGuiCol_TableBorderStrong] = imgui_color(palette.border_subtle);
  colors[ImGuiCol_TableBorderLight] = imgui_color(palette.border_subtle);
  colors[ImGuiCol_TableRowBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
  colors[ImGuiCol_TableRowBgAlt] = imgui_color(palette.layer);
  colors[ImGuiCol_BorderShadow] = {0.0F, 0.0F, 0.0F, 0.0F};
  colors[ImGuiCol_FrameBg] = imgui_color(palette.surface);
  colors[ImGuiCol_FrameBgHovered] = imgui_color(palette.layer_hover);
  colors[ImGuiCol_FrameBgActive] = imgui_color(palette.selected);
  colors[ImGuiCol_TitleBg] = imgui_color(palette.surface);
  colors[ImGuiCol_TitleBgActive] = imgui_color(palette.surface);
  colors[ImGuiCol_MenuBarBg] = imgui_color(palette.surface);
  colors[ImGuiCol_ScrollbarBg] = imgui_color(palette.layer);
  colors[ImGuiCol_ScrollbarGrab] = imgui_color(palette.border_strong);
  colors[ImGuiCol_ScrollbarGrabHovered] = imgui_color(palette.action_hover);
  colors[ImGuiCol_ScrollbarGrabActive] = imgui_color(palette.action_pressed);
  colors[ImGuiCol_CheckMark] = imgui_color(palette.action);
  colors[ImGuiCol_SliderGrab] = imgui_color(palette.action);
  colors[ImGuiCol_SliderGrabActive] = imgui_color(palette.action_pressed);
  colors[ImGuiCol_Button] = imgui_color(palette.surface);
  colors[ImGuiCol_ButtonHovered] = imgui_color(palette.layer_hover);
  colors[ImGuiCol_ButtonActive] = imgui_color(palette.selected);
  colors[ImGuiCol_Header] = imgui_color(palette.selected);
  colors[ImGuiCol_HeaderHovered] = imgui_color(palette.layer_hover);
  colors[ImGuiCol_HeaderActive] = imgui_color(palette.action_soft);
  colors[ImGuiCol_Separator] = imgui_color(palette.border_subtle);
  colors[ImGuiCol_SeparatorHovered] = imgui_color(palette.border_interactive);
  colors[ImGuiCol_SeparatorActive] = imgui_color(palette.action);
  colors[ImGuiCol_ResizeGrip] = imgui_color(palette.border_subtle);
  colors[ImGuiCol_ResizeGripHovered] = imgui_color(palette.action_hover);
  colors[ImGuiCol_ResizeGripActive] = imgui_color(palette.action_pressed);
  colors[ImGuiCol_Tab] = imgui_color(palette.layer);
  colors[ImGuiCol_TabHovered] = imgui_color(palette.layer_hover);
  colors[ImGuiCol_TabSelected] = imgui_color(palette.surface);
  colors[ImGuiCol_TabDimmed] = imgui_color(palette.layer);
  colors[ImGuiCol_TabDimmedSelected] = imgui_color(palette.surface);
  colors[ImGuiCol_TextSelectedBg] = imgui_color(palette.selection);
  colors[ImGuiCol_NavCursor] = imgui_color(palette.focus);
  applied_dark_theme_ = dark;
  applied_high_contrast_ = high_contrast;
}

void App::cycle_theme() {
  if (shell_state_.theme == ThemePreference::system) {
    shell_state_.theme = ThemePreference::light;
  } else if (shell_state_.theme == ThemePreference::light) {
    shell_state_.theme = ThemePreference::dark;
  } else {
    shell_state_.theme = ThemePreference::system;
  }
  settings_.theme = shell_state_.theme;
  apply_theme();
  persist_settings();
  persist_shell_state();
}

void App::apply_settings_live(bool rebuild_fonts) {
  shell_state_.theme = settings_.theme;
  if (editor_) {
    auto preferences = editor_->preferences();
    preferences.soft_wrap = settings_.editor.wrap;
    editor_->set_preferences(preferences);
  }
  ai_context_bytes_ = settings_.ai.context_bytes;
  if (options_.repository_path && assistant_) {
    auto key = ConfiguredSecret{};
    if (settings_.ai.enabled) {
      key = anthropic_api_key();
    }
    const auto secret = key.value;
    auto config = AiConfig{.enabled = settings_.ai.enabled,
                           .context_bytes = settings_.ai.context_bytes,
                           .model = settings_.ai.model,
                           .max_tokens = settings_.ai.max_tokens};
    assistant_ = std::make_unique<Assistant>(config, key.configured, [secret] {
      return std::make_unique<AnthropicProvider>(secret);
    });
  }
  if (rebuild_fonts && initialized_) {
    fonts_rebuild_requested_ = true;
  }
  apply_theme();
}

void App::persist_settings() {
  if (screenshot_mode()) {
    return;
  }
  auto path = user_settings_path();
  if (settings_scope_ == SettingsScope::repository &&
      options_.repository_path) {
    path = repository_settings_path(*options_.repository_path);
  }
  const auto saved = save_settings(path, settings_);
  if (!saved) {
    editor_status_ = saved.error().message;
    settings_diagnostics_ = {
        SettingsDiagnostic{.path = path, .message = saved.error().message}};
    return;
  }
  settings_diagnostics_.clear();
  editor_status_ = "settings applied";
}

void App::reset_settings(SettingsGroup group) {
  reset_settings_group(settings_, group);
  apply_settings_live(group == SettingsGroup::appearance);
  persist_settings();
}

void App::toggle_high_contrast() {
  shell_state_.high_contrast = !shell_state_.high_contrast;
  apply_theme();
  persist_shell_state();
}

void App::persist_shell_state() {
  if (!options_.repository_path || screenshot_mode()) {
    return;
  }
  shell_state_.panel_layout = panels_.state();
  const auto error =
      save_app_shell_state(*options_.repository_path, shell_state_);
  if (!error.empty()) {
    editor_status_ = error;
  }
}

void App::reset_panel_layout() {
  reset_app_shell_layout(shell_state_);
  panels_.restore(shell_state_.panel_layout);
  editor_status_ = "panel layout reset";
  persist_shell_state();
}

void App::update_window_title() {
  auto repository_name = std::string{"Welcome"};
  if (options_.repository_path &&
      first_run_.phase() == RepositoryLifecyclePhase::open) {
    repository_name = options_.repository_path->filename().string();
    if (repository_name.empty()) {
      repository_name = options_.repository_path->string();
    }
  } else if (screenshot_mode()) {
    repository_name = "liquify";
  }
  auto change_name = std::string{"Working change"};
  if (review_) {
    const auto snapshot = review_->snapshot();
    if (snapshot.review.selected) {
      change_name = "Change " + snapshot.review.selected->id;
    }
  } else if (screenshot_mode()) {
    change_name = "Change #31";
  }
  const auto title = repository_name + " — " + change_name + " — liquify";
  if (title != window_title_) {
    SDL_SetWindowTitle(window_, title.c_str());
    window_title_ = title;
  }
}

void App::capture_screenshot() {
  if (screenshot_written_ || screenshot_failed_ ||
      !options_.screenshot_output) {
    return;
  }
  auto *pixels = SDL_RenderReadPixels(renderer_, nullptr);
  if (pixels == nullptr) {
    std::fprintf(stderr, "liquify: screenshot readback failed: %s\n",
                 SDL_GetError());
    screenshot_failed_ = true;
    return;
  }
  auto *rgba = SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(pixels);
  if (rgba == nullptr) {
    std::fprintf(stderr, "liquify: screenshot conversion failed: %s\n",
                 SDL_GetError());
    screenshot_failed_ = true;
    return;
  }
  const auto width = rgba->w;
  const auto height = rgba->h;
  auto image = ScreenshotImage{};
  image.width = static_cast<std::uint32_t>(width);
  image.height = static_cast<std::uint32_t>(height);
  image.rgba.reserve(static_cast<std::size_t>(width) *
                     static_cast<std::size_t>(height) * 4);
  const auto *source = static_cast<const std::uint8_t *>(rgba->pixels);
  for (int row = 0; row < height; ++row) {
    const auto offset =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(rgba->pitch);
    image.rgba.insert(image.rgba.end(), source + offset,
                      source + offset + width * 4);
  }
  SDL_DestroySurface(rgba);
  const auto written = write_png(*options_.screenshot_output, image);
  if (!written) {
    std::fprintf(stderr, "liquify: screenshot failed: %s\n",
                 written.error().c_str());
    screenshot_failed_ = true;
    return;
  }
  screenshot_written_ = true;
  std::printf("screenshot: state=%s size=%dx%d out=%s\n",
              options_.screenshot_state->c_str(), width, height,
              options_.screenshot_output->string().c_str());
}

bool App::confirm_discard(std::string_view action, bool active_only) {
  auto dirty = std::vector<EditorBuffer *>{};
  auto message = std::string{"Save changes before continuing?\n\n"};
  for (auto &[path, open] : open_buffers_) {
    if (open.buffer->dirty() &&
        (!active_only || open.buffer.get() == editor_)) {
      dirty.push_back(open.buffer.get());
      message += path + "\n";
    }
  }
  if (dirty.empty()) {
    return true;
  }
  if (!window_) {
    editor_status_ = "a native window is required to confirm unsaved changes";
    return false;
  }
  const SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Save"},
      {0, 2, "Discard"},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
  };
  const auto title = std::string(action);
  const SDL_MessageBoxData dialog = {SDL_MESSAGEBOX_WARNING,
                                     window_,
                                     title.c_str(),
                                     message.c_str(),
                                     static_cast<int>(std::size(buttons)),
                                     buttons,
                                     nullptr};
  auto selected = 0;
  if (!SDL_ShowMessageBox(&dialog, &selected)) {
    editor_status_ =
        std::string{"unable to confirm unsaved changes: "} + SDL_GetError();
    return false;
  }
  if (selected == 2) {
    return true;
  }
  if (selected != 1) {
    return false;
  }
  for (auto *buffer : dirty) {
    if (!save_buffer(*buffer)) {
      const auto error =
          std::string(buffer->display_path()) + ": " + editor_status_;
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save failed",
                               error.c_str(), window_);
      return false;
    }
  }
  return true;
}

void App::close_editor() {
  if (!editor_ || !confirm_discard("Close file", true)) {
    return;
  }
  const auto path = std::string(editor_->display_path());
  auto *closing = editor_;
  if (editor_graph_ && !review_diff_open_) {
    editor_graph_->close_buffer(std::filesystem::path(path));
  }
  editor_ = nullptr;
  const auto found = open_buffers_.find(path);
  if (found != open_buffers_.end() && found->second.buffer.get() == closing) {
    open_buffers_.erase(found);
  }
  transient_editor_.reset();
  layouts_.reset();
  text_layer_.reset();
  analysis_buffer_open_ = false;
  analysis_text_.clear();
  review_diff_open_ = false;
  editor_focused_ = false;
  if (!open_buffers_.empty()) {
    open_editor(open_buffers_.begin()->first, "retained");
  }
}

void App::request_quit() {
  if (confirm_discard("Quit liquify")) {
    quit_ = true;
  }
}

void App::undo_editor(bool redo) {
  if (!editor_ || review_diff_open_ || !editor_mutation_allowed()) {
    return;
  }
  const auto change = redo ? editor_->redo() : editor_->undo();
  if (change) {
    apply_change(change->first_line, change->erased_lines,
                 change->inserted_lines);
  }
}

void App::refresh_saved_buffer(const std::string &path) {
  const auto found = open_buffers_.find(path);
  if (found == open_buffers_.end() || found->second.buffer->dirty() ||
      !options_.repository_path) {
    return;
  }
  auto loaded =
      EditorBuffer::open(*options_.repository_path, path, "saved:" + path);
  if (!loaded.error().empty()) {
    editor_status_ = std::string(loaded.error());
    return;
  }
  auto &buffer = *found->second.buffer;
  const auto change = buffer.refresh_saved_text(loaded.text());
  if (change && &buffer == editor_) {
    apply_change(change->first_line, change->erased_lines,
                 change->inserted_lines);
  } else if (change && editor_graph_) {
    const auto language = stub_language_for_path(path);
    if (language) {
      editor_graph_->open_buffer(path, *language, buffer.text());
    }
  }
}

void App::remember_editor_view() {
  if (!editor_) {
    return;
  }
  const auto found = open_buffers_.find(std::string(editor_->display_path()));
  if (found != open_buffers_.end() && found->second.buffer.get() == editor_) {
    found->second.scroll_line = scroll_line_;
    found->second.horizontal_scroll = horizontal_scroll_;
  }
}

void App::open_editor(std::string path, std::string blob_id,
                      bool record_history) {
  if (!options_.repository_path) {
    return;
  }
  path = std::filesystem::path(path).lexically_normal().generic_string();
  auto found = open_buffers_.find(path);
  if (found != open_buffers_.end() && found->second.buffer.get() == editor_) {
    repository_focused_ = false;
    editor_focused_ = true;
    return;
  }
  if (found == open_buffers_.end()) {
    auto buffer = std::make_unique<EditorBuffer>(EditorBuffer::open(
        *options_.repository_path, path, std::move(blob_id)));
    if (!buffer->error().empty()) {
      editor_status_ = std::string(buffer->error());
      return;
    }
    found = open_buffers_.emplace(path, OpenBuffer{std::move(buffer)}).first;
  }
  remember_editor_view();
  if (record_history) {
    if (const auto current = current_navigation_location()) {
      navigation_history_.replace_current(*current);
    }
  }
  editor_ = found->second.buffer.get();
  const auto cursor = editor_->cursor();
  const auto location = NavigationLocation{path, cursor.line, cursor.byte};
  if (record_history) {
    navigation_history_.visit(location);
  } else {
    navigation_history_.replace_current(location);
  }
  repository_focused_ = false;
  analysis_buffer_open_ = false;
  analysis_text_.clear();
  review_diff_open_ = false;
  structural_selection_history_.clear();
  structural_references_.clear();
  auto preferences = editor_->preferences();
  preferences.soft_wrap = settings_.editor.wrap;
  editor_->set_preferences(preferences);
  layouts_ = std::make_unique<TextLayoutCache>();
  layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
  text_layer_ = std::make_unique<RetainedTextLayer>();
  text_layer_->reset(editor_->line_count());
  scroll_line_ = found->second.scroll_line;
  horizontal_scroll_ = found->second.horizontal_scroll;
  column_selection_anchor_.reset();
  buffer_matches_ = editor_->find(find_query_, find_options_);
  find_match_index_ = 0;
  editor_focused_ = true;
  editor_status_.clear();
  if (!editor_graph_ || !editor_->error().empty()) {
    return;
  }
  auto language =
      stub_language_for_path(std::filesystem::path(editor_->display_path()));
  if (!language) {
    return;
  }
  analysis_text_ = editor_->text();
  analysis_buffer_open_ = true;
  editor_graph_->open_buffer(std::filesystem::path(editor_->display_path()),
                             *language, analysis_text_);
  editor_graph_->request_diagnostics(
      std::filesystem::path(editor_->display_path()));
  editor_graph_->request_editor_view(
      std::filesystem::path(editor_->display_path()), *language,
      analysis_text_);
}

auto App::current_navigation_location() const
    -> std::optional<NavigationLocation> {
  if (!editor_) {
    return std::nullopt;
  }
  const auto cursor = editor_->cursor();
  return NavigationLocation{std::filesystem::path(editor_->display_path()),
                            cursor.line, cursor.byte};
}

void App::open_navigation_location(const NavigationLocation &location,
                                   bool record_history) {
  auto blob = std::string{"navigation:"} + location.path.generic_string();
  if (repository_) {
    if (const auto *path =
            repository_->metadata(location.path.generic_string());
        path != nullptr && !path->blob_id.empty()) {
      blob = path->blob_id;
    }
  }
  open_editor(location.path.generic_string(), std::move(blob), record_history);
  if (!editor_ || !editor_->error().empty() ||
      editor_->display_path() !=
          location.path.lexically_normal().generic_string()) {
    return;
  }
  editor_->set_cursor({static_cast<std::size_t>(location.line),
                       static_cast<std::size_t>(location.column)});
  navigation_history_.replace_current(location);
  ensure_cursor_visible();
}

void App::navigate_history(bool forward) {
  if (const auto current = current_navigation_location()) {
    navigation_history_.replace_current(*current);
  }
  auto location =
      forward ? navigation_history_.forward() : navigation_history_.back();
  if (location) {
    open_navigation_location(*location, false);
  }
}

auto App::navigation_symbol_at_cursor() const -> const NavigationSymbol * {
  if (!repository_ || !editor_) {
    return nullptr;
  }
  const auto byte = editor_->byte_offset(editor_->cursor());
  const auto path = std::filesystem::path(editor_->display_path());
  const auto symbols = repository_->navigation_symbols();
  const auto occurrence =
      std::ranges::find_if(symbols, [&](const auto &symbol) {
        return symbol.path == path && symbol.range.start_byte <= byte &&
               byte <= symbol.range.end_byte;
      });
  if (occurrence != symbols.end()) {
    return &*occurrence;
  }
  if (const auto *enclosing = enclosing_symbol(symbols, path, byte)) {
    return enclosing;
  }
  const auto name = identifier_at(editor_->text(), byte);
  const auto named = std::ranges::find_if(symbols, [&](const auto &symbol) {
    return symbol.name == name && symbol.definition;
  });
  return named == symbols.end() ? nullptr : &*named;
}

void App::show_symbol_references() {
  const auto *symbol = navigation_symbol_at_cursor();
  if (symbol == nullptr || !repository_) {
    editor_status_ = "no indexed symbol at cursor";
    return;
  }
  navigation_results_ = symbol_references(repository_->navigation_symbols(),
                                          symbol->id, symbol->name);
  navigation_results_title_ = "References — " + symbol->name;
  navigation_results_open_ = true;
}

void App::show_call_hierarchy() {
  const auto *symbol = navigation_symbol_at_cursor();
  if (symbol == nullptr || !repository_) {
    editor_status_ = "no indexed symbol at cursor";
    return;
  }
  navigation_hierarchy_ = call_hierarchy(repository_->navigation_symbols(),
                                         symbol->id, symbol->name);
  navigation_results_title_ = "Call hierarchy — " + symbol->name;
  navigation_results_.clear();
  navigation_results_open_ = true;
}

void App::reveal_repository_path() {
  if (!options_.repository_path || repository_action_source_.empty()) {
    return;
  }
  auto target_path = std::filesystem::absolute(*options_.repository_path /
                                               repository_action_source_)
                         .lexically_normal();
  if (!std::filesystem::exists(target_path)) {
    target_path =
        std::filesystem::absolute(*options_.repository_path).lexically_normal();
  }
  const auto target = target_path.generic_string();
  const auto url = "file://" + target;
  if (!SDL_OpenURL(url.c_str())) {
    editor_status_ = std::string{"unable to reveal path: "} + SDL_GetError();
  }
}

void App::invoke_navigation_action(NavigationAction action) {
  switch (action) {
  case NavigationAction::quick_open:
    quick_open_open_ = true;
    return;
  case NavigationAction::symbol_search:
    symbol_search_open_ = true;
    return;
  case NavigationAction::go_to_definition:
    request_definition();
    return;
  case NavigationAction::find_references:
    show_symbol_references();
    return;
  case NavigationAction::call_hierarchy:
    show_call_hierarchy();
    return;
  case NavigationAction::focus_breadcrumbs:
    breadcrumb_focus_requested_ = true;
    editor_focused_ = false;
    return;
  case NavigationAction::open_recent:
    open_recent_open_ = true;
    return;
  case NavigationAction::back:
    navigate_history(false);
    return;
  case NavigationAction::forward:
    navigate_history(true);
    return;
  case NavigationAction::open:
    if (!repository_selection_.empty() && !repository_selection_blob_.empty()) {
      open_editor(repository_selection_, repository_selection_blob_);
    }
    return;
  case NavigationAction::rename:
  case NavigationAction::move:
    if (!repository_selection_.empty()) {
      repository_action_source_ = repository_selection_;
      repository_action_target_ = repository_selection_;
      repository_move_open_ = true;
    }
    return;
  case NavigationAction::new_file:
    if (!repository_selection_.empty()) {
      auto base = std::filesystem::path(repository_selection_);
      if (!repository_selection_blob_.empty()) {
        base = base.parent_path();
      }
      repository_action_target_ = (base / "new-file.txt").generic_string();
      repository_new_file_open_ = true;
    }
    return;
  case NavigationAction::reveal:
    repository_action_source_ = repository_selection_;
    reveal_repository_path();
    return;
  }
}

void App::show_review_diff(const ReviewSnapshot &review) {
  if (!review.selected || !review.head) {
    return;
  }
  remember_editor_view();
  analysis_buffer_open_ = false;
  analysis_text_.clear();
  review_diff_lines_ = review.diff;
  auto text = std::string{};
  for (std::size_t index = 0; index < review_diff_lines_.size(); ++index) {
    if (index != 0) {
      text.push_back('\n');
    }
    text += review_diff_lines_[index].text;
  }
  const auto title = "Review " + review.selected->id + " — text diff";
  transient_editor_ = std::make_unique<EditorBuffer>(EditorBuffer::from_text(
      "review:" + review.selected->id, title, std::move(text)));
  editor_ = transient_editor_.get();
  layouts_ = std::make_unique<TextLayoutCache>();
  layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
  text_layer_ = std::make_unique<RetainedTextLayer>();
  text_layer_->reset(editor_->line_count());
  review_diff_open_ = true;
  displayed_review_head_ = liquid::to_string(review.head->value);
  pending_review_id_.clear();
  scroll_line_ = 0;
  horizontal_scroll_ = 0.0F;
  editor_focused_ = true;
  editor_status_ =
      std::format("text diff ready in {:.3f} ms", review.open_to_first_hunk_ms);
}

void App::open_review(std::string id) {
  if (!review_) {
    return;
  }
  const auto snapshot = review_->snapshot();
  if (snapshot.review.selected && snapshot.review.selected->id == id &&
      !snapshot.busy) {
    show_review_diff(snapshot.review);
    return;
  }
  pending_review_id_ = id;
  review_->request_open(std::move(id));
  editor_status_ = "pulling review";
}

void App::navigate_hunk(bool forward) {
  if (!review_) {
    return;
  }
  if (!review_diff_open_) {
    const auto snapshot = review_->snapshot();
    if (!snapshot.review.selected) {
      return;
    }
    show_review_diff(snapshot.review);
  }
  if (!editor_) {
    return;
  }
  const auto current = editor_->cursor().line;
  if (forward) {
    for (auto line = current + 1; line < review_diff_lines_.size(); ++line) {
      if (review_diff_lines_[line].kind == ReviewDiffLineKind::hunk) {
        editor_->set_cursor({line, 0});
        ensure_cursor_visible();
        return;
      }
    }
  } else {
    for (auto line = current; line > 0; --line) {
      if (review_diff_lines_[line - 1].kind == ReviewDiffLineKind::hunk) {
        editor_->set_cursor({line - 1, 0});
        ensure_cursor_visible();
        return;
      }
    }
  }
}

void App::navigate_comment() {
  if (!review_) {
    return;
  }
  const auto snapshot = review_->snapshot();
  if (snapshot.review.comments.empty()) {
    editor_status_ = "review has no comments";
    return;
  }
  next_comment_index_ %= snapshot.review.comments.size();
  const auto &comment = snapshot.review.comments[next_comment_index_++];
  if (comment.state == ReviewAnchorState::deleted) {
    editor_status_ = "deleted anchor parked: " + comment.value.body;
    return;
  }
  if (review_diff_open_ && editor_) {
    const auto found =
        std::ranges::find_if(review_diff_lines_, [&comment](const auto &line) {
          return line.path == comment.value.anchor.path && line.new_line &&
                 *line.new_line == comment.value.anchor.line;
        });
    if (found != review_diff_lines_.end()) {
      const auto line =
          static_cast<std::size_t>(found - review_diff_lines_.begin());
      editor_->set_cursor({line, 0});
      ensure_cursor_visible();
      return;
    }
  }
  open_editor(comment.value.anchor.path, "review:" + comment.value.anchor.path);
  if (editor_ && editor_->error().empty()) {
    const auto line =
        comment.value.anchor.line == 0 ? 0 : comment.value.anchor.line - 1;
    const auto column =
        comment.value.anchor.column == 0 ? 0 : comment.value.anchor.column - 1;
    editor_->set_cursor(
        {static_cast<std::size_t>(line), static_cast<std::size_t>(column)});
    ensure_cursor_visible();
  }
}

void App::begin_review_comment() {
  if (!review_ || !review_->snapshot().review.selected) {
    editor_status_ = "open a review before commenting";
    return;
  }
  review_comment_body_.clear();
  review_comment_popup_open_ = true;
}

void App::submit_review_comment() {
  if (!review_ || !editor_ || review_comment_body_.empty()) {
    return;
  }
  auto path = std::string{};
  auto line = std::uint64_t{};
  auto column = std::uint64_t{};
  const auto cursor = editor_->cursor();
  if (review_diff_open_) {
    if (cursor.line >= review_diff_lines_.size() ||
        !review_diff_lines_[cursor.line].new_line) {
      editor_status_ = "comments require a current-side diff line";
      return;
    }
    const auto &diff_line = review_diff_lines_[cursor.line];
    path = diff_line.path;
    line = *diff_line.new_line;
    column = cursor.byte > 2 ? cursor.byte - 1 : 1;
  } else {
    path = std::string(editor_->display_path());
    line = cursor.line + 1;
    column = cursor.byte + 1;
  }
  review_->request_comment(review_comment_body_, std::move(path), line, column);
  review_comment_body_.clear();
  editor_status_ = "posting review comment";
}

void App::poll_review() {
  if (!review_) {
    return;
  }
  const auto snapshot = review_->snapshot();
  if (snapshot.generation != review_generation_) {
    review_generation_ = snapshot.generation;
    if (!snapshot.error.empty()) {
      editor_status_ = snapshot.error;
    } else if (snapshot.review.selected && snapshot.review.head) {
      const auto head = liquid::to_string(snapshot.review.head->value);
      if (pending_review_id_ == snapshot.review.selected->id ||
          (!displayed_review_head_.empty() && displayed_review_head_ != head)) {
        show_review_diff(snapshot.review);
      }
    }
  }
  const auto now = std::chrono::steady_clock::now();
  if (snapshot.review.selected && !snapshot.busy &&
      now >= next_review_refresh_) {
    review_->request_refresh();
    next_review_refresh_ = now + std::chrono::seconds{2};
  }
}

void App::apply_change(std::size_t first_line, std::size_t erased_lines,
                       std::size_t inserted_lines) {
  const BufferChange change{first_line, erased_lines, inserted_lines};
  layouts_->apply_change(change);
  text_layer_->apply_change(change);
  editor_status_.clear();
  sync_editor_graph();
}

void App::save_editor() {
  if (editor_ && !review_diff_open_) {
    static_cast<void>(save_buffer(*editor_));
  }
}

bool App::save_buffer(EditorBuffer &buffer) {
  if (settings_.editor.format_on_save &&
      std::filesystem::path(buffer.display_path()).extension() == ".liquid") {
    auto document =
        liquid::parse_configuration(buffer.text(), buffer.display_path());
    if (!document) {
      editor_status_ = document.error().message;
      return false;
    }
    auto formatted = liquid::format_configuration(*document);
    if (!formatted) {
      editor_status_ = formatted.error().message;
      return false;
    }
    if (*formatted != buffer.text()) {
      buffer.select_all();
      const auto change = buffer.insert_text(*formatted);
      if (change && &buffer == editor_) {
        apply_change(change->first_line, change->erased_lines,
                     change->inserted_lines);
      }
    }
  }
  const auto build_running =
      editor_graph_ && editor_graph_->snapshot().build_running;
  auto saved = false;
  if (options_.repository_path) {
    auto repository_options = liquid::repository_options{};
    repository_options.session = multiplayer_session_;
    auto repository = liquid::runtime::block_on(liquid::repository::open(
        *options_.repository_path, repository_options));
    if (!repository) {
      editor_status_ = repository.error().message;
      return false;
    }
    saved = buffer.save(*repository);
  } else {
    saved = buffer.save();
  }
  if (!saved) {
    editor_status_ = std::string(buffer.error());
    return false;
  }
  editor_status_ = "saved as a drip";
  if (editor_graph_) {
    editor_graph_->note_saved_drip(buffer.pending_drips());
    editor_graph_->refresh_status();
    if (build_running) {
      request_build();
      editor_status_ = "saved as a drip; build replanned";
    }
  }
  return true;
}

void App::sync_editor_graph() {
  if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
    return;
  }
  auto current = editor_->text();
  auto edit = source_edit_between(analysis_text_, current);
  if (!edit) {
    if (analysis_text_ != current) {
      editor_status_ = "analysis overlay exceeds the supported byte range";
    }
    return;
  }
  const auto path = std::filesystem::path(editor_->display_path());
  editor_graph_->apply_edit(path, std::move(*edit));
  analysis_text_ = std::move(current);
  editor_graph_->request_diagnostics(path);
  const auto language = stub_language_for_path(path);
  if (language) {
    editor_graph_->request_editor_view(path, *language, analysis_text_);
  }
}

void App::request_definition() {
  if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
    return;
  }
  const auto byte = editor_->byte_offset(editor_->cursor());
  if (byte <= std::numeric_limits<std::uint32_t>::max()) {
    pending_definition_origin_ = current_navigation_location();
    editor_graph_->request_definition(
        std::filesystem::path(editor_->display_path()),
        static_cast<std::uint32_t>(byte));
  }
}

void App::request_hover() {
  if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
    return;
  }
  const auto byte = editor_->byte_offset(editor_->cursor());
  if (byte <= std::numeric_limits<std::uint32_t>::max()) {
    editor_graph_->request_hover(std::filesystem::path(editor_->display_path()),
                                 static_cast<std::uint32_t>(byte));
  }
}

void App::request_completion() {
  if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
    return;
  }
  const auto byte = editor_->byte_offset(editor_->cursor());
  if (byte <= std::numeric_limits<std::uint32_t>::max()) {
    editor_graph_->request_completion(
        std::filesystem::path(editor_->display_path()),
        static_cast<std::uint32_t>(byte));
  }
}

void App::request_build() {
  if (editor_graph_) {
    editor_graph_->request_build();
  }
}

void App::request_tests(std::string filter) {
  if (editor_graph_) {
    editor_graph_->request_tests(std::move(filter));
  }
}

void App::request_push() {
  if (editor_graph_) {
    editor_graph_->request_push(
        repository_ ? std::string(repository_->lake_remote()) : std::string{});
  }
}

void App::expand_structural_selection() {
  if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
    return;
  }
  const auto cursor = editor_->cursor();
  const auto byte = editor_->byte_offset(cursor);
  if (byte > std::numeric_limits<std::uint32_t>::max()) {
    return;
  }
  auto current = std::optional<SourceRange>{};
  if (const auto selection = editor_->selection()) {
    current = SourceRange{
        static_cast<std::uint32_t>(editor_->byte_offset(selection->begin)),
        static_cast<std::uint32_t>(editor_->byte_offset(selection->end)),
        static_cast<std::uint32_t>(selection->begin.line),
        static_cast<std::uint32_t>(selection->begin.byte),
        static_cast<std::uint32_t>(selection->end.line),
        static_cast<std::uint32_t>(selection->end.byte)};
    structural_selection_history_.push_back(*current);
  } else {
    structural_selection_history_.push_back(
        {static_cast<std::uint32_t>(byte), static_cast<std::uint32_t>(byte),
         static_cast<std::uint32_t>(cursor.line),
         static_cast<std::uint32_t>(cursor.byte),
         static_cast<std::uint32_t>(cursor.line),
         static_cast<std::uint32_t>(cursor.byte)});
  }
  editor_graph_->request_structural_selection(
      std::filesystem::path(editor_->display_path()),
      static_cast<std::uint32_t>(byte), current, false);
  editor_status_ = "expanding structural selection";
}

void App::shrink_structural_selection() {
  if (!editor_ || structural_selection_history_.empty()) {
    return;
  }
  const auto range = structural_selection_history_.back();
  structural_selection_history_.pop_back();
  editor_->set_cursor({range.start_line, range.start_column});
  if (range.start_byte != range.end_byte) {
    editor_->set_cursor({range.end_line, range.end_column}, true);
  }
  ensure_cursor_visible();
  editor_status_.clear();
}

void App::select_enclosing_definition() {
  if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
    return;
  }
  const auto cursor = editor_->cursor();
  const auto byte = editor_->byte_offset(cursor);
  if (byte > std::numeric_limits<std::uint32_t>::max()) {
    return;
  }
  editor_graph_->request_structural_selection(
      std::filesystem::path(editor_->display_path()),
      static_cast<std::uint32_t>(byte), std::nullopt, true);
  editor_status_ = "selecting enclosing definition";
}

void App::select_symbol_references() {
  if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
    return;
  }
  const auto byte = editor_->byte_offset(editor_->cursor());
  if (byte > std::numeric_limits<std::uint32_t>::max()) {
    return;
  }
  editor_graph_->request_reference_selection(
      std::filesystem::path(editor_->display_path()),
      static_cast<std::uint32_t>(byte));
  editor_status_ = "selecting symbol references";
}

void App::open_rename() {
  if (!analysis_buffer_open_) {
    return;
  }
  structural_name_.clear();
  rename_open_ = true;
}

void App::open_extract_function() {
  if (!analysis_buffer_open_ || !editor_ || !editor_->selection()) {
    editor_status_ = "extract-function requires a statement selection";
    return;
  }
  structural_name_.clear();
  extract_function_open_ = true;
}

void App::apply_structural_preview(bool all_files) {
  if (!editor_graph_) {
    return;
  }
  auto accepted = std::vector<StructuralEdit>{};
  for (const auto &edit : structural_preview_) {
    const auto path = edit.target.path.generic_string();
    if (all_files || std::ranges::find(structural_accepted_files_, path) !=
                         structural_accepted_files_.end()) {
      accepted.push_back(edit);
    }
  }
  if (accepted.empty()) {
    editor_status_ = "no structural edit files selected";
    return;
  }
  editor_graph_->apply_structural_edits(std::move(accepted));
  editor_status_ = "applying structural edits";
}

void App::request_assistant_context(std::optional<AssistantAction> action) {
  if (!assistant_ || !editor_graph_ || !analysis_buffer_open_ || !editor_) {
    return;
  }
  if (action && !assistant_->snapshot().enabled) {
    editor_status_ = "AI is off";
    return;
  }
  const auto cursor = editor_->cursor();
  const auto cursor_byte = editor_->byte_offset(cursor);
  if (cursor_byte > std::numeric_limits<std::uint32_t>::max() ||
      editor_->text().size() > std::numeric_limits<std::uint32_t>::max()) {
    editor_status_ = "assistant context exceeds the supported byte range";
    return;
  }
  auto selection_range = std::optional<SourceRange>{};
  auto selected_source = std::string{};
  if (const auto selection = editor_->selection()) {
    const auto start = editor_->byte_offset(selection->begin);
    const auto end = editor_->byte_offset(selection->end);
    selection_range =
        SourceRange{static_cast<std::uint32_t>(start),
                    static_cast<std::uint32_t>(end),
                    static_cast<std::uint32_t>(selection->begin.line),
                    static_cast<std::uint32_t>(selection->begin.byte),
                    static_cast<std::uint32_t>(selection->end.line),
                    static_cast<std::uint32_t>(selection->end.byte)};
    selected_source = editor_->text().substr(start, end - start);
  }
  const auto snapshot = editor_graph_->snapshot();
  if (snapshot.history_generation != history_generation_) {
    history_generation_ = snapshot.history_generation;
    if (!snapshot.history_error.empty()) {
      editor_status_ = snapshot.history_error;
    } else if (editor_) {
      const auto path = std::string(editor_->display_path());
      refresh_saved_buffer(path);
      editor_status_ = "repository edit history applied";
    }
  }
  ai_context_generation_ = snapshot.ai_context_generation;
  pending_assistant_action_ = action;
  pending_assistant_prompt_ = assistant_prompt_;
  if (action == AssistantAction::explain_diagnostic &&
      pending_assistant_prompt_.empty()) {
    pending_assistant_prompt_ = "Explain the diagnostic at the cursor.";
  } else if (action == AssistantAction::propose_edit &&
             pending_assistant_prompt_.empty()) {
    pending_assistant_prompt_ = "Propose a focused correction.";
  }
  context_preview_open_ = !action.has_value();
  editor_graph_->request_ai_context(
      std::filesystem::path(editor_->display_path()),
      static_cast<std::uint32_t>(cursor_byte), selection_range,
      std::move(selected_source), editor_->text().size(), ai_context_bytes_);
  editor_status_ = "building graph context";
}

void App::accept_assistant_edit() {
  if (!assistant_ || !editor_ || !options_.repository_path) {
    return;
  }
  if (!editor_mutation_allowed()) {
    editor_status_ = "edit refused: another session holds a lease";
    return;
  }
  assistant_->accept();
  const auto accepted = assistant_->take_accepted();
  if (!accepted) {
    return;
  }
  const auto change = editor_->apply_edit(*accepted);
  if (!change) {
    editor_status_ = "assistant edit no longer matches this buffer";
    assistant_->reject();
    return;
  }
  apply_change(change->first_line, change->erased_lines,
               change->inserted_lines);
  save_editor();
  if (editor_->dirty()) {
    return;
  }
  const auto recorded = record_assisted_edit(
      *options_.repository_path, std::filesystem::path(editor_->display_path()),
      *accepted);
  if (!recorded) {
    editor_status_ = recorded.error().message;
  } else {
    editor_status_ = "accepted assisted edit as one drip";
  }
}

void App::toggle_assistant() {
  if (!assistant_ || !options_.repository_path) {
    return;
  }
  const auto enable = !assistant_->snapshot().enabled;
  const auto saved =
      set_repository_ai_enabled(*options_.repository_path, enable);
  if (!saved) {
    editor_status_ = saved.error().message;
    return;
  }
  settings_.ai.enabled = enable;
  if (!enable) {
    assistant_->set_enabled(false);
    editor_status_ = "AI off";
    return;
  }
  auto config = load_ai_config(*options_.repository_path);
  if (!config) {
    editor_status_ = config.error().message;
    return;
  }
  config->enabled = true;
  ai_context_bytes_ = config->context_bytes;
  auto key = anthropic_api_key();
  const auto secret = key.value;
  assistant_ = std::make_unique<Assistant>(*config, key.configured, [secret] {
    return std::make_unique<AnthropicProvider>(secret);
  });
  editor_status_ = key.configured ? "AI on" : "AI on; key not configured";
}

void App::poll_editor_graph() {
  if (!editor_graph_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now >= next_status_refresh_) {
    editor_graph_->refresh_status();
    next_status_refresh_ = now + std::chrono::milliseconds{500};
  }
  const auto snapshot = editor_graph_->snapshot();
  if (snapshot.structural_selection_generation !=
      structural_selection_generation_) {
    structural_selection_generation_ = snapshot.structural_selection_generation;
    if (snapshot.structural_selection && editor_) {
      const auto &range = *snapshot.structural_selection;
      editor_->set_cursor({range.start_line, range.start_column});
      editor_->set_cursor({range.end_line, range.end_column}, true);
      ensure_cursor_visible();
      editor_status_.clear();
    }
  }
  if (snapshot.structural_references_generation !=
      structural_references_generation_) {
    structural_references_generation_ =
        snapshot.structural_references_generation;
    structural_references_ = snapshot.structural_references;
    editor_status_ = structural_references_.empty()
                         ? "no symbol references found"
                         : "selected " +
                               std::to_string(structural_references_.size()) +
                               " symbol references";
    if (!structural_references_.empty()) {
      panels_.focus(PanelId::structural_editing);
      persist_shell_state();
    }
  }
  if (snapshot.structural_preview_generation !=
      structural_preview_generation_) {
    structural_preview_generation_ = snapshot.structural_preview_generation;
    structural_preview_.clear();
    structural_diagnostics_.clear();
    structural_unresolved_.clear();
    structural_accepted_files_.clear();
    if (snapshot.rename_preview) {
      structural_preview_ = snapshot.rename_preview->edits;
      for (const auto &unresolved : snapshot.rename_preview->unresolved) {
        structural_unresolved_.push_back(
            unresolved.path.generic_string() + ":" +
            std::to_string(unresolved.range.start_line + 1) + ":" +
            std::to_string(unresolved.range.start_column + 1));
      }
    } else if (snapshot.extract_preview) {
      structural_preview_ = snapshot.extract_preview->edits;
      structural_diagnostics_ = snapshot.extract_preview->diagnostics;
    }
    for (const auto &edit : structural_preview_) {
      const auto path = edit.target.path.generic_string();
      if (std::ranges::find(structural_accepted_files_, path) ==
          structural_accepted_files_.end()) {
        structural_accepted_files_.push_back(path);
      }
    }
    structural_preview_open_ = true;
    panels_.focus(PanelId::structural_editing);
    persist_shell_state();
    editor_status_ = structural_preview_.empty()
                         ? "structural preview has diagnostics"
                         : "structural preview ready";
  }
  if (snapshot.structural_result_generation != structural_result_generation_) {
    structural_result_generation_ = snapshot.structural_result_generation;
    if (snapshot.structural_result) {
      const auto applied = snapshot.structural_result->applied.size();
      const auto refused = snapshot.structural_result->refused.size();
      editor_status_ = "structural edits applied=" + std::to_string(applied) +
                       " refused=" + std::to_string(refused);
      structural_preview_open_ = false;
      structural_preview_.clear();
      for (const auto &item : snapshot.structural_result->applied) {
        refresh_saved_buffer(item.path.generic_string());
      }
    }
  }
  if (snapshot.ai_context_generation != ai_context_generation_) {
    ai_context_generation_ = snapshot.ai_context_generation;
    if (snapshot.ai_context) {
      context_preview_ = snapshot.ai_context->text;
      context_omissions_ = snapshot.ai_context->omissions;
      if (pending_assistant_action_) {
        assistant_->submit(*pending_assistant_action_,
                           std::move(pending_assistant_prompt_),
                           *snapshot.ai_context, snapshot.ai_context_focus);
        pending_assistant_action_.reset();
      }
      editor_status_.clear();
    }
  }
  if (snapshot.definition_generation != definition_generation_) {
    definition_generation_ = snapshot.definition_generation;
    // liquify#78: a definition opens only a path inside the repository.
    if (snapshot.definition &&
        (snapshot.definition->path.is_absolute() ||
         std::ranges::any_of(snapshot.definition->path,
                             [](const auto &part) { return part == ".."; }))) {
      editor_status_ = "go to definition: the result is not a path inside the repository";
    } else if (snapshot.definition) {
      const auto target = snapshot.definition->path.generic_string();
      if (pending_definition_origin_) {
        navigation_history_.replace_current(*pending_definition_origin_);
      }
      const auto &range = snapshot.definition->declaration.range;
      const auto location = NavigationLocation{
          snapshot.definition->path, range.start_line, range.start_column};
      navigation_history_.visit(location);
      if (!editor_ || editor_->display_path() != target) {
        open_editor(target, "graph:" + target, false);
      }
      editor_->set_cursor({range.start_line, range.start_column});
      navigation_history_.replace_current(location);
      ensure_cursor_visible();
    }
    pending_definition_origin_.reset();
  }
}

void App::handle_text(std::string_view text) {
  if (!editor_ || !editor_focused_ || review_diff_open_ ||
      editor_->mode() == EditingMode::modal_command) {
    return;
  }
  // A modal dialog owns the keyboard: its fields receive the text through ImGui, and the
  // source it covers must not (liquify#39).
  if (ImGui::GetCurrentContext() != nullptr &&
      ImGui::GetTopMostPopupModal() != nullptr) {
    return;
  }
  if (!editor_mutation_allowed()) {
    return;
  }
  const auto change = editor_->insert_text(text);
  if (change) {
    apply_change(change->first_line, change->erased_lines,
                 change->inserted_lines);
  }
}

void App::handle_composition(std::string_view text, int selection_start,
                             int selection_length) {
  if (!editor_ || !editor_focused_ || review_diff_open_ ||
      editor_->mode() == EditingMode::modal_command) {
    input_.cancel_composition();
    return;
  }
  input_.update_composition(std::string(text), selection_start,
                            selection_length);
}

void App::copy_editor_selection(bool cut) {
  if (!editor_ || !editor_focused_) {
    return;
  }
  const auto selection = editor_->selection();
  if (!selection) {
    return;
  }
  const auto source = editor_->text();
  const auto first = editor_->byte_offset(selection->begin);
  const auto last = editor_->byte_offset(selection->end);
  const auto selected = source.substr(first, last - first);
  auto *payload = new ClipboardPayload(
      make_clipboard_payload(selected, std::string(editor_->display_path())));
  const char *mime_types[] = {clipboard_plain_utf8, clipboard_plain,
                              clipboard_html};
  if (!SDL_SetClipboardData(clipboard_data, clipboard_cleanup, payload,
                            mime_types, std::size(mime_types))) {
    delete payload;
    editor_status_ = std::string{"clipboard copy failed: "} + SDL_GetError();
    return;
  }
  if (!cut || review_diff_open_ || !editor_mutation_allowed()) {
    return;
  }
  const auto change = editor_->backspace();
  if (change) {
    apply_change(change->first_line, change->erased_lines,
                 change->inserted_lines);
  }
}

void App::paste_editor_clipboard() {
  if (!editor_ || !editor_focused_ || review_diff_open_ ||
      !editor_mutation_allowed()) {
    return;
  }
  auto plain = std::optional<std::string>{};
  if (SDL_HasClipboardData(clipboard_plain_utf8)) {
    plain = clipboard_value(clipboard_plain_utf8);
  } else if (SDL_HasClipboardData(clipboard_plain)) {
    plain = clipboard_value(clipboard_plain);
  } else if (SDL_HasClipboardText()) {
    auto *value = SDL_GetClipboardText();
    if (value != nullptr) {
      plain = value;
      SDL_free(value);
    }
  }
  auto html = std::optional<std::string>{};
  if (!plain && SDL_HasClipboardData(clipboard_html)) {
    html = clipboard_value(clipboard_html);
  }
  const auto value = preferred_clipboard_text(
      plain ? std::optional<std::string_view>{*plain} : std::nullopt,
      html ? std::optional<std::string_view>{*html} : std::nullopt);
  if (!value) {
    editor_status_ = "clipboard does not contain supported text";
    return;
  }
  handle_text(*value);
}

void App::handle_file_drop(const std::filesystem::path &source,
                           InputPoint point) {
  const auto target = input_.update_drop_target(
      point, repository_drop_rectangle_, editor_drop_rectangle_);
  if (!options_.repository_path || !repository_) {
    editor_status_ = "open a repository before dropping files";
    return;
  }
  std::error_code filesystem_error;
  if (!std::filesystem::is_regular_file(source, filesystem_error)) {
    editor_status_ = "only regular files can be dropped";
    return;
  }
  if (target == DropTarget::none) {
    editor_status_ = "drop the file on the repository tree or source editor";
    return;
  }
  auto destination = source.filename();
  if (target == DropTarget::editor && editor_) {
    destination = std::filesystem::path(editor_->display_path()).parent_path() /
                  destination;
  }
  auto opened = liquid::runtime::block_on(
      liquid::repository::open(*options_.repository_path));
  if (!opened) {
    editor_status_ = opened.error().message;
    return;
  }
  auto written = liquid::runtime::block_on(
      opened->write_file(destination.generic_string(), source));
  if (!written) {
    editor_status_ = written.error().message;
    return;
  }
  repository_ = std::make_unique<RepositoryView>(
      RepositoryView::open(*options_.repository_path));
  if (target == DropTarget::editor) {
    const auto path = destination.generic_string();
    refresh_saved_buffer(path);
    open_editor(path, "drop:" + path);
  }
  editor_status_ = "imported " + destination.generic_string();
}

void App::handle_key(std::uint32_t key, std::uint16_t modifiers) {
  const auto primary = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
  const auto shift = (modifiers & SDL_KMOD_SHIFT) != 0;
  const auto alt = (modifiers & SDL_KMOD_ALT) != 0;
  if (command_matches(settings_, "file.close", "Primary+W", key, modifiers)) {
    close_editor();
    return;
  }
  if (primary && key == SDLK_Q) {
    request_quit();
    return;
  }
  if (command_matches(settings_, "settings.open", "Primary+,", key,
                      modifiers)) {
    settings_open_ = true;
    return;
  }
  if (primary && key == SDLK_B) {
    shell_state_.sidebar_visible = !shell_state_.sidebar_visible;
    persist_shell_state();
    return;
  }
  if (primary && key == SDLK_J) {
    shell_state_.bottom_panel_visible = !shell_state_.bottom_panel_visible;
    persist_shell_state();
    return;
  }
  if (primary && key == SDLK_BACKSLASH) {
    shell_state_.editor_split = !shell_state_.editor_split;
    persist_shell_state();
    return;
  }
  if (primary && shift && key == SDLK_T) {
    cycle_theme();
    return;
  }
  if (command_matches(settings_, "palette.open", "Primary+Shift+P", key,
                      modifiers)) {
    command_palette_open_ = true;
    return;
  }
  if (primary && key == SDLK_P) {
    invoke_navigation_action(NavigationAction::quick_open);
    return;
  }
  if (primary && key == SDLK_T) {
    invoke_navigation_action(NavigationAction::symbol_search);
    return;
  }
  if (primary && !alt && key == SDLK_R) {
    invoke_navigation_action(NavigationAction::open_recent);
    return;
  }
  if (alt && key == SDLK_LEFT) {
    invoke_navigation_action(NavigationAction::back);
    return;
  }
  if (alt && key == SDLK_RIGHT) {
    invoke_navigation_action(NavigationAction::forward);
    return;
  }
  if (primary && shift && key == SDLK_PERIOD) {
    invoke_navigation_action(NavigationAction::focus_breadcrumbs);
    return;
  }
  if (shift && key == SDLK_F12) {
    invoke_navigation_action(NavigationAction::find_references);
    return;
  }
  if (alt && key == SDLK_F12) {
    invoke_navigation_action(NavigationAction::call_hierarchy);
    return;
  }
  if (key == SDLK_F2) {
    invoke_navigation_action(NavigationAction::rename);
    return;
  }
  if (primary && shift && key == SDLK_M) {
    invoke_navigation_action(NavigationAction::move);
    return;
  }
  if (primary && key == SDLK_N) {
    invoke_navigation_action(NavigationAction::new_file);
    return;
  }
  if (primary && alt && key == SDLK_R) {
    invoke_navigation_action(NavigationAction::reveal);
    return;
  }
  if (repository_focused_ && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
    invoke_navigation_action(NavigationAction::open);
    return;
  }
  if (command_matches(settings_, "build.run", "F7", key, modifiers)) {
    request_build();
    return;
  }
  if (command_matches(settings_, "tests.run", "F8", key, modifiers)) {
    request_tests();
    return;
  }
  if (command_matches(settings_, "push.run", "F9", key, modifiers)) {
    request_push();
    return;
  }
  if (command_matches(settings_, "change.cut", "Primary+Shift+K", key,
                      modifiers)) {
    static_cast<void>(invoke_command("change.cut"));
    return;
  }
  if (!editor_) {
    return;
  }
  if (!editor_focused_) {
    return;
  }
  if (primary && shift && key == SDLK_F) {
    panels_.focus(PanelId::search);
    panels_.clear_focus_request();
    shell_state_.sidebar_visible = true;
    persist_shell_state();
    return;
  }
  if (primary && key == SDLK_F) {
    find_open_ = true;
    buffer_matches_ = editor_->find(find_query_, find_options_);
    return;
  }
  if (primary && key == SDLK_Z) {
    undo_editor(shift);
    return;
  }
  if (primary && key == SDLK_L) {
    editor_->select(SelectionKind::line);
    return;
  }
  if (primary && alt && (key == SDLK_UP || key == SDLK_DOWN)) {
    const auto cursor = editor_->cursor();
    if (key == SDLK_UP && cursor.line > 0) {
      editor_->add_cursor({cursor.line - 1, cursor.byte});
    } else if (key == SDLK_DOWN && cursor.line + 1 < editor_->line_count()) {
      editor_->add_cursor({cursor.line + 1, cursor.byte});
    }
    return;
  }
  if (primary && alt && key == SDLK_C) {
    begin_review_comment();
    return;
  }
  if (primary && alt && key == SDLK_A) {
    if (review_) {
      review_->request_approve();
    }
    return;
  }
  if (alt && key == SDLK_DOWN) {
    navigate_hunk(true);
    return;
  }
  if (alt && key == SDLK_UP) {
    navigate_hunk(false);
    return;
  }
  if (alt && key == SDLK_C) {
    navigate_comment();
    return;
  }
  if (review_diff_open_) {
    if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP ||
        key == SDLK_DOWN || key == SDLK_HOME || key == SDLK_END ||
        key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
      // Navigation continues through the common cursor handling below.
    } else {
      return;
    }
  }
  if (primary && shift && key == SDLK_RIGHT) {
    expand_structural_selection();
    return;
  }
  if (primary && shift && key == SDLK_LEFT) {
    shrink_structural_selection();
    return;
  }
  if (primary && shift && key == SDLK_D) {
    select_enclosing_definition();
    return;
  }
  if (primary && shift && key == SDLK_R) {
    open_rename();
    return;
  }
  if (primary && shift && key == SDLK_X) {
    open_extract_function();
    return;
  }
  if (primary && shift && key == SDLK_L) {
    toggle_assistant();
    return;
  }
  if (primary && shift && key == SDLK_A) {
    request_assistant_context(AssistantAction::ask);
    return;
  }
  if (primary && shift && key == SDLK_E) {
    request_assistant_context(AssistantAction::explain_diagnostic);
    return;
  }
  if (primary && shift && key == SDLK_I) {
    request_assistant_context(AssistantAction::propose_edit);
    return;
  }
  if (primary && shift && key == SDLK_C) {
    request_assistant_context(std::nullopt);
    return;
  }
  if (primary && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
    accept_assistant_edit();
    return;
  }
  if (primary && key == SDLK_BACKSPACE) {
    if (assistant_) {
      assistant_->reject();
    }
    return;
  }
  if (command_matches(settings_, "file.save", "Primary+S", key, modifiers)) {
    if (review_diff_open_) {
      editor_status_ = "review diffs are read-only";
      return;
    }
    save_editor();
    return;
  }
  if (key == SDLK_F12) {
    request_definition();
    return;
  }
  if (key == SDLK_F1) {
    request_hover();
    return;
  }
  if (primary && key == SDLK_SPACE) {
    request_completion();
    return;
  }
  if (primary && key == SDLK_A) {
    editor_->select_all();
    return;
  }
  if (primary && key == SDLK_M) {
    const auto next = editor_->mode() == EditingMode::standard
                          ? EditingMode::modal_command
                          : EditingMode::standard;
    editor_->set_mode(next);
    return;
  }

  if (editor_->mode() == EditingMode::modal_command) {
    if (key == SDLK_I) {
      editor_->set_mode(EditingMode::modal_insert);
    } else if (key == SDLK_A) {
      editor_->move_right();
      editor_->set_mode(EditingMode::modal_insert);
    } else if (key == SDLK_H) {
      editor_->move_left(shift);
    } else if (key == SDLK_J) {
      editor_->move_down(shift);
    } else if (key == SDLK_K) {
      editor_->move_up(shift);
    } else if (key == SDLK_L) {
      editor_->move_right(shift);
    } else if (key == SDLK_0) {
      editor_->move_home(shift);
    } else if (key == SDLK_4 && shift) {
      editor_->move_end();
    } else if (key == SDLK_X) {
      if (!editor_mutation_allowed()) {
        return;
      }
      const auto change = editor_->delete_forward();
      if (change) {
        apply_change(change->first_line, change->erased_lines,
                     change->inserted_lines);
      }
    }
    ensure_cursor_visible();
    return;
  }

  if (key == SDLK_ESCAPE && editor_->mode() == EditingMode::modal_insert) {
    editor_->set_mode(EditingMode::modal_command);
  } else if (key == SDLK_LEFT) {
    editor_->move_left(shift);
  } else if (key == SDLK_RIGHT) {
    editor_->move_right(shift);
  } else if (key == SDLK_UP) {
    editor_->move_up(shift);
  } else if (key == SDLK_DOWN) {
    editor_->move_down(shift);
  } else if (key == SDLK_HOME) {
    editor_->move_home(shift);
  } else if (key == SDLK_END) {
    editor_->move_end(shift);
  } else if (key == SDLK_PAGEUP) {
    const auto cursor = editor_->cursor();
    const auto amount = std::max<std::size_t>(visible_lines_, 1);
    editor_->set_cursor(
        {cursor.line > amount ? cursor.line - amount : 0, cursor.byte}, shift);
  } else if (key == SDLK_PAGEDOWN) {
    const auto cursor = editor_->cursor();
    const auto amount = std::max<std::size_t>(visible_lines_, 1);
    editor_->set_cursor({cursor.line + amount, cursor.byte}, shift);
  } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
    if (!editor_mutation_allowed()) {
      return;
    }
    const auto change = editor_->insert_newline();
    apply_change(change.first_line, change.erased_lines, change.inserted_lines);
  } else if (key == SDLK_TAB) {
    if (!editor_mutation_allowed()) {
      return;
    }
    const auto change =
        editor_->insert_text(indentation_text(editor_->preferences()));
    if (change) {
      apply_change(change->first_line, change->erased_lines,
                   change->inserted_lines);
    }
  } else if (key == SDLK_BACKSPACE) {
    if (!editor_mutation_allowed()) {
      return;
    }
    const auto change = editor_->backspace();
    if (change) {
      apply_change(change->first_line, change->erased_lines,
                   change->inserted_lines);
    }
  } else if (key == SDLK_DELETE) {
    if (!editor_mutation_allowed()) {
      return;
    }
    const auto change = editor_->delete_forward();
    if (change) {
      apply_change(change->first_line, change->erased_lines,
                   change->inserted_lines);
    }
  }
  ensure_cursor_visible();
}

void App::ensure_cursor_visible() {
  if (!editor_ || visible_lines_ == 0) {
    return;
  }
  const auto line = editor_->cursor().line;
  if (line < scroll_line_) {
    scroll_line_ = line;
  } else if (line >= scroll_line_ + visible_lines_) {
    scroll_line_ = line - visible_lines_ + 1;
  }
}

void App::position_cursor(float x, float y, bool extend_selection) {
  if (!editor_ || editor_height_ <= 0.0F) {
    return;
  }
  const auto visible_line = static_cast<std::size_t>(
      std::max(0.0F, std::floor((y - editor_y_) / line_height_)));
  const auto line =
      std::min(scroll_line_ + visible_line, editor_->line_count() - 1);
  const auto column = std::max(0.0F, (x - editor_x_ - editor_gutter_width -
                                      editor_padding + horizontal_scroll_) /
                                         cell_width_);
  const auto &layout = layouts_->line(line, editor_->line(line));
  const auto byte = layout.byte_for_column(column, editor_->line(line).size());
  editor_->set_cursor({line, byte}, extend_selection);
}

void App::update_text_input_area() {
  if (!editor_ || !editor_visible_) {
    return;
  }
  const auto cursor = editor_->cursor();
  const auto row = cursor.line >= scroll_line_ ? cursor.line - scroll_line_ : 0;
  const auto &layout = layouts_->line(cursor.line, editor_->line(cursor.line));
  const auto column = layout.column_for_byte(cursor.byte);
  const auto x = editor_x_ + editor_gutter_width + editor_padding +
                 column * cell_width_ - horizontal_scroll_;
  const auto y = editor_y_ + static_cast<float>(row) * line_height_;
  const auto area = SDL_Rect{static_cast<int>(x), static_cast<int>(y),
                             std::max(static_cast<int>(cell_width_), 1),
                             std::max(static_cast<int>(line_height_), 1)};
  SDL_SetTextInputArea(window_, &area, 0);
}

void App::draw_composition() {
  const auto &composition = input_.composition();
  if (!composition.active() || !editor_) {
    return;
  }
  const auto cursor = editor_->cursor();
  if (cursor.line < scroll_line_ ||
      cursor.line >= scroll_line_ + visible_lines_) {
    return;
  }
  const auto &layout = layouts_->line(cursor.line, editor_->line(cursor.line));
  const auto column = layout.column_for_byte(cursor.byte);
  const auto x = editor_x_ + editor_gutter_width + editor_padding +
                 column * cell_width_ - horizontal_scroll_;
  const auto y =
      editor_y_ + static_cast<float>(cursor.line - scroll_line_) * line_height_;
  const auto selection_start =
      utf8_byte_offset(composition.text, composition.selection_start);
  const auto selection_end =
      utf8_byte_offset(composition.text, composition.selection_start +
                                             composition.selection_length);
  const auto prefix_width =
      ImGui::CalcTextSize(composition.text.data(),
                          composition.text.data() + selection_start)
          .x;
  const auto selection_width =
      ImGui::CalcTextSize(composition.text.data() + selection_start,
                          composition.text.data() + selection_end)
          .x;
  const auto text_size = ImGui::CalcTextSize(composition.text.c_str());
  const auto palette =
      effective_palette(applied_dark_theme_, applied_high_contrast_);
  auto *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(
      ImVec2(x, y), ImVec2(x + text_size.x + design::space1, y + line_height_),
      packed_color(palette.overlay));
  if (selection_width > 0.0F) {
    draw_list->AddRectFilled(
        ImVec2(x + prefix_width, y),
        ImVec2(x + prefix_width + selection_width, y + line_height_),
        packed_color(palette.selected));
  }
  draw_list->AddText(ImVec2(x, y + 2.0F), packed_color(palette.text_primary),
                     composition.text.c_str());
  draw_list->AddLine(ImVec2(x, y + line_height_ - 1.0F),
                     ImVec2(x + text_size.x, y + line_height_ - 1.0F),
                     packed_color(palette.focus),
                     applied_high_contrast_ ? 3.0F : 2.0F);
}

void App::draw_repository_tree() {
  ImGui::TextUnformatted(repository_->name().c_str());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", options_.repository_path->string().c_str());
  }
  ImGui::Separator();
  if (!repository_->error().empty()) {
    draw_user_error({.kind = FailureKind::local,
                     .summary = "This repository could not be opened",
                     .next_step = "Reopen it from the front door.",
                     .details = without_absolute_paths(repository_->error(),
                                                       repository_->path())},
                    "repository-failure");
    return;
  }
  repository_->poll();
  if (repository_->navigation_loading()) {
    ImGui::TextDisabled("Indexing paths and symbols...");
  } else if (!repository_->navigation_error().empty()) {
    draw_user_error(describe_index_failure(repository_->navigation_error(),
                                           repository_->path()),
                    "index-failure");
  }
  if (!repository_->mutation_status().empty()) {
    ImGui::TextWrapped("%s",
                       std::string(repository_->mutation_status()).c_str());
  }
  if (repository_->entries().empty()) {
    ImGui::TextWrapped("This repository has no files.");
    return;
  }
  draw_repository_entries({});
}

void App::draw_review_panel() {
  ImGui::TextUnformatted("Reviews");
  if (!review_) {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(
        "A review asks people to read a change on lake before it lands. "
        "Reviews live on the lake this repository pushes to, and it has none "
        "yet.");
    ImGui::PopTextWrapPos();
    if (repository_ && kit_button("Set lake", ButtonKind::primary)) {
      open_set_lake_dialog();
    }
    frame_layout_.controls["review/set-lake"] = item_rectangle();
    return;
  }
  const auto snapshot = review_->snapshot();
  if (ImGui::SmallButton("Request review")) {
    open_request_review();
  }
  frame_layout_.controls["review/request"] = item_rectangle();
  ImGui::SameLine();
  if (ImGui::SmallButton("Refresh reviews")) {
    if (snapshot.review.selected) {
      review_->request_refresh();
    } else {
      review_->request_list();
    }
  }
  if (snapshot.busy) {
    ImGui::SameLine();
    ImGui::TextDisabled("loading...");
  }
  if (!snapshot.error.empty()) {
    draw_user_error(
        {.kind = FailureKind::other,
         .summary = "Lake did not complete the review request",
         .next_step = "Reviews need the change on lake: push it, then refresh "
                      "or request the review again.",
         .details = without_absolute_paths(snapshot.error, repository_path())},
        "review-failure");
  }
  for (const auto &item : snapshot.review.reviews) {
    const auto label =
        item.id + " [" + std::string(review_state_name(item.state)) + "]";
    const auto selected =
        snapshot.review.selected && snapshot.review.selected->id == item.id;
    if (ImGui::Selectable(label.c_str(), selected)) {
      open_review(item.id);
    }
  }
  if (!snapshot.review.selected) {
    if (!snapshot.busy && snapshot.error.empty() &&
        snapshot.review.reviews.empty()) {
      ImGui::PushTextWrapPos(0.0F);
      ImGui::TextUnformatted(
          "No reviews yet. A review asks people to read a change before it "
          "lands: cut your work as a change, push it, then request a review.");
      ImGui::PopTextWrapPos();
      if (kit_button("Request review", ButtonKind::primary)) {
        open_request_review();
      }
      frame_layout_.controls["review/request-empty"] = item_rectangle();
    }
    return;
  }
  ImGui::Separator();
  ImGui::TextWrapped(
      "Review %s: %s", snapshot.review.selected->id.c_str(),
      std::string(review_state_name(snapshot.review.selected->state)).c_str());
  if (ImGui::SmallButton("New comment")) {
    begin_review_comment();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Approve")) {
    review_->request_approve();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Request changes")) {
    review_->request_changes();
  }

  ImGui::Text("Comments (%zu)", snapshot.review.comments.size());
  for (std::size_t index = 0; index < snapshot.review.comments.size();
       ++index) {
    const auto &comment = snapshot.review.comments[index];
    const auto label = comment.value.anchor.path + ":" +
                       std::to_string(comment.value.anchor.line) + " [" +
                       std::string(review_anchor_state_name(comment.state)) +
                       "] " + comment.value.body + "##review-comment-" +
                       std::to_string(index);
    if (ImGui::Selectable(label.c_str())) {
      next_comment_index_ = index;
      navigate_comment();
    }
  }
}

void App::draw_review_comment_popup() {
  if (review_comment_popup_open_) {
    ImGui::OpenPopup("Review Comment");
    review_comment_popup_open_ = false;
  }
  if (!begin_dialog("Review Comment", ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::TextUnformatted("Comment on the current cursor");
  ImGui::InputTextMultiline("##review-comment-body", &review_comment_body_,
                            ImVec2(520.0F, 140.0F));
  ImGui::BeginDisabled(review_comment_body_.empty());
  if (ImGui::Button("Post comment")) {
    submit_review_comment();
    if (review_comment_body_.empty()) {
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    review_comment_body_.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::draw_repository_entries(std::string_view directory) {
  const auto entries = directory.empty() ? repository_->entries()
                                         : repository_->entries(directory);
  for (const auto &entry : entries) {
    const auto path = directory.empty()
                          ? entry.name
                          : std::string(directory) + "/" + entry.name;
    const auto *metadata = repository_->metadata(path);
    const auto kind =
        metadata == nullptr
            ? navigation_file_kind(path, entry.kind == liquid::node_kind::tree)
            : metadata->kind;
    const auto tree_row = entry.kind == liquid::node_kind::tree;
    const auto tree_spacing = ImGui::GetTreeNodeToLabelSpacing();
    auto label = tree_row ? icon_label(entry.name, tree_spacing, tree_spacing)
                          : icon_label(entry.name, 0.0F);
    if (metadata != nullptr &&
        metadata->change != NavigationChangeState::clean) {
      label += "  [" +
               std::string(navigation_change_state_name(metadata->change)) +
               "]";
    }
    if (metadata != nullptr &&
        metadata->drive != NavigationDriveState::unavailable) {
      label += "  [" +
               std::string(navigation_drive_state_name(metadata->drive)) + "]";
    }
    label += "##" + path;
    ImGui::PushID(path.c_str());
    if (entry.kind == liquid::node_kind::blob) {
      if (ImGui::Selectable(label.c_str(),
                            editor_ && editor_->display_path() == path)) {
        repository_selection_ = path;
        repository_selection_blob_ = liquid::to_string(entry.id);
        repository_focused_ = true;
        open_editor(path, liquid::to_string(entry.id));
      }
      frame_layout_.controls["repository/" + path] = item_rectangle();
      item_icon(file_icon(kind));
      file_type_hint(kind);
    } else if (entry.kind == liquid::node_kind::tree) {
      const auto open =
          ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_OpenOnArrow);
      item_icon(file_icon(kind), ImGui::GetTreeNodeToLabelSpacing());
      file_type_hint(kind);
      if (ImGui::IsItemClicked()) {
        repository_selection_ = path;
        repository_selection_blob_.clear();
        repository_focused_ = true;
        editor_focused_ = false;
      }
      if (open) {
        if (!repository_->directory_loaded(path) &&
            !repository_->directory_loading(path)) {
          repository_->request_directory(path, entry.ref);
        }
        if (repository_->directory_loading(path)) {
          ImGui::TextDisabled("loading...");
        } else if (!repository_->directory_error(path).empty()) {
          draw_user_error(
              describe_folder_failure(path, repository_->directory_error(path),
                                      repository_->path()),
              "folder-failure/" + path);
        } else {
          draw_repository_entries(path);
        }
        ImGui::TreePop();
      }
    } else {
      ImGui::Text("%s (%s)", entry.name.c_str(),
                  std::string(node_kind_name(entry.kind)).c_str());
    }
    if (ImGui::BeginPopupContextItem("repository-actions")) {
      repository_selection_ = path;
      repository_selection_blob_ = entry.kind == liquid::node_kind::blob
                                       ? liquid::to_string(entry.id)
                                       : std::string{};
      repository_action_source_ = path;
      if (entry.kind == liquid::node_kind::blob && ImGui::MenuItem("Open")) {
        invoke_navigation_action(NavigationAction::open);
      }
      if (ImGui::MenuItem("Rename")) {
        repository_action_target_ = path;
        repository_move_open_ = true;
      }
      if (ImGui::MenuItem("Move")) {
        repository_action_target_ = path;
        repository_move_open_ = true;
      }
      if (ImGui::MenuItem("New File")) {
        auto base = std::filesystem::path(path);
        if (entry.kind == liquid::node_kind::blob) {
          base = base.parent_path();
        }
        repository_action_target_ = (base / "new-file.txt").generic_string();
        repository_new_file_open_ = true;
      }
      if (ImGui::MenuItem("Reveal")) {
        reveal_repository_path();
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
}

void App::draw_navigation_popups() {
  const auto *screenshot =
      options_.screenshot_state
          ? find_screenshot_state(*options_.screenshot_state)
          : nullptr;
  const auto navigation_screenshot =
      screenshot != nullptr && screenshot->view == ScreenshotView::navigation;
  if (quick_open_open_) {
    ImGui::OpenPopup("Quick Open");
    quick_open_open_ = false;
  }
  if (begin_dialog("Quick Open", ImGuiWindowFlags_AlwaysAutoResize)) {
    editor_visible_ = false;
    ImGui::SetNextItemWidth(620.0F);
    const auto quick_open_flags = navigation_screenshot
                                      ? ImGuiInputTextFlags_ReadOnly
                                      : ImGuiInputTextFlags_None;
    ImGui::InputTextWithHint("##quick-open", "Type a path", &quick_open_query_,
                             quick_open_flags);
    frame_layout_.controls["quick-open/query"] = item_rectangle();
    if (navigation_screenshot) {
      file_item("src/app.cpp", NavigationFileKind::cpp_source, true);
      file_item("src/app_state.cpp", NavigationFileKind::cpp_source);
      file_item("docs/ui/app-shell.md", NavigationFileKind::markdown);
      ImGui::TextDisabled("3 path matches · Primary+P");
    } else if (!repository_) {
      ImGui::TextDisabled("Open a repository to search paths.");
    } else if (repository_->navigation_loading()) {
      ImGui::TextDisabled("Indexing repository paths...");
    } else {
      for (const auto &match :
           rank_paths(repository_->navigation_paths(), quick_open_query_)) {
        const auto &path = repository_->navigation_paths()[match.index];
        const auto label = icon_label(path.path.generic_string(), 0.0F) +
                           "##quick-" + std::to_string(match.index);
        if (ImGui::Selectable(label.c_str())) {
          open_editor(path.path.generic_string(), path.blob_id);
          quick_open_query_.clear();
          ImGui::CloseCurrentPopup();
        }
        frame_layout_.controls["quick-open/" + path.path.generic_string()] =
            item_rectangle();
        item_icon(file_icon(path.kind));
        file_type_hint(path.kind);
      }
    }
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();
    }
    frame_layout_.controls["quick-open/close"] = item_rectangle();
    ImGui::EndPopup();
  }

  if (symbol_search_open_) {
    ImGui::OpenPopup("Symbol Search");
    symbol_search_open_ = false;
  }
  if (begin_dialog("Symbol Search", ImGuiWindowFlags_AlwaysAutoResize)) {
    editor_visible_ = false;
    ImGui::SetNextItemWidth(620.0F);
    ImGui::InputTextWithHint("##symbol-search", "Type a symbol",
                             &symbol_search_query_);
    if (!repository_) {
      ImGui::TextDisabled(
          "Open a repository to search libliquid symbol records.");
    } else if (repository_->navigation_loading()) {
      ImGui::TextDisabled("Indexing symbol records...");
    } else {
      for (const auto &match : rank_symbols(repository_->navigation_symbols(),
                                            symbol_search_query_)) {
        const auto &symbol = repository_->navigation_symbols()[match.index];
        const auto label = symbol.name + " — " + symbol.path.generic_string() +
                           ":" + std::to_string(symbol.range.start_line + 1) +
                           "##symbol-" + std::to_string(match.index);
        if (ImGui::Selectable(label.c_str())) {
          open_navigation_location({symbol.path, symbol.range.start_line,
                                    symbol.range.start_column});
          symbol_search_query_.clear();
          ImGui::CloseCurrentPopup();
        }
      }
    }
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (open_recent_open_) {
    ImGui::OpenPopup("Open Recent");
    open_recent_open_ = false;
  }
  if (begin_dialog("Open Recent", ImGuiWindowFlags_AlwaysAutoResize)) {
    editor_visible_ = false;
    if (navigation_history_.recent().empty()) {
      ImGui::TextDisabled("No recently opened paths.");
    }
    for (std::size_t index = 0; index < navigation_history_.recent().size();
         ++index) {
      const auto &location = navigation_history_.recent()[index];
      const auto label = location.path.generic_string() + ":" +
                         std::to_string(location.line + 1) + "##recent-" +
                         std::to_string(index);
      if (ImGui::Selectable(label.c_str())) {
        open_navigation_location(location);
        ImGui::CloseCurrentPopup();
      }
    }
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::draw_repository_action_popup() {
  if (repository_move_open_) {
    ImGui::OpenPopup("Move or Rename Path");
    repository_move_open_ = false;
  }
  if (begin_dialog("Move or Rename Path", ImGuiWindowFlags_AlwaysAutoResize)) {
    editor_visible_ = false;
    ImGui::Text("Source: %s", repository_action_source_.c_str());
    ImGui::SetNextItemWidth(520.0F);
    text_field("Destination", &repository_action_target_);
    ImGui::BeginDisabled(!repository_ || repository_action_target_.empty() ||
                         repository_->mutation_running());
    if (ImGui::Button("Apply")) {
      repository_->request_move(repository_action_source_,
                                repository_action_target_);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (repository_new_file_open_) {
    ImGui::OpenPopup("New File");
    repository_new_file_open_ = false;
  }
  if (begin_dialog("New File", ImGuiWindowFlags_AlwaysAutoResize)) {
    editor_visible_ = false;
    ImGui::SetNextItemWidth(520.0F);
    text_field("Path", &repository_action_target_);
    ImGui::BeginDisabled(!repository_ || repository_action_target_.empty() ||
                         repository_->mutation_running());
    if (ImGui::Button("Create")) {
      repository_->request_new_file(repository_action_target_);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::draw_navigation_results() {
  if (!navigation_results_open_) {
    return;
  }
  editor_visible_ = false;
  auto open = true;
  if (ImGui::Begin(navigation_results_title_.c_str(), &open)) {
    const auto draw_symbol = [this](const NavigationSymbol &symbol,
                                    std::string_view prefix) {
      const auto label = std::string(prefix) + symbol.name + " — " +
                         symbol.path.generic_string() + ":" +
                         std::to_string(symbol.range.start_line + 1) +
                         "##nav-result-" + symbol.id +
                         symbol.path.generic_string() +
                         std::to_string(symbol.range.start_byte);
      if (ImGui::Selectable(label.c_str())) {
        open_navigation_location(
            {symbol.path, symbol.range.start_line, symbol.range.start_column});
      }
    };
    if (!navigation_results_.empty()) {
      for (const auto &symbol : navigation_results_) {
        draw_symbol(symbol,
                    symbol.definition ? "Definition  " : "Reference   ");
      }
    } else {
      ImGui::TextUnformatted("Callers");
      for (const auto &symbol : navigation_hierarchy_.callers) {
        draw_symbol(symbol, "");
      }
      if (navigation_hierarchy_.callers.empty()) {
        ImGui::TextDisabled("No indexed callers.");
      }
      ImGui::Separator();
      ImGui::TextUnformatted("Callees");
      for (const auto &symbol : navigation_hierarchy_.callees) {
        draw_symbol(symbol, "");
      }
      if (navigation_hierarchy_.callees.empty()) {
        ImGui::TextDisabled("No indexed callees.");
      }
    }
  }
  ImGui::End();
  if (!open) {
    navigation_results_open_ = false;
  }
}

void App::draw_assistant() {
  if (!assistant_) {
    return;
  }
  const auto snapshot = assistant_->snapshot();
  ImGui::Text("Assistant: %s", snapshot.status.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("What Will Be Sent")) {
    request_assistant_context(std::nullopt);
  }
  ImGui::InputTextWithHint("##assistant-prompt",
                           "Ask about the selection or current definition",
                           &assistant_prompt_);
  ImGui::BeginDisabled(!snapshot.enabled || snapshot.running);
  if (ImGui::Button("Ask")) {
    request_assistant_context(AssistantAction::ask);
  }
  ImGui::SameLine();
  if (ImGui::Button("Explain diagnostic")) {
    request_assistant_context(AssistantAction::explain_diagnostic);
  }
  ImGui::SameLine();
  if (ImGui::Button("Propose edit")) {
    request_assistant_context(AssistantAction::propose_edit);
  }
  ImGui::EndDisabled();
  if (!snapshot.enabled) {
    ImGui::TextUnformatted(
        "AI is off. No provider exists and no connection can be opened.");
  } else if (!snapshot.key_configured) {
    ImGui::TextUnformatted("Anthropic key: not configured");
  } else {
    ImGui::TextUnformatted("Anthropic key: configured");
  }
  if (snapshot.refusal) {
    ImGui::TextColored(ImVec4(0.65F, 0.30F, 0.05F, 1.0F), "Refusal: %s",
                       snapshot.response.c_str());
  } else if (!snapshot.response.empty()) {
    ImGui::TextWrapped("%s", snapshot.response.c_str());
  }
  if (snapshot.proposal) {
    ImGui::Separator();
    ImGui::Text("Proposed replacement for bytes %u..%u",
                snapshot.proposal->start_byte, snapshot.proposal->old_end_byte);
    ImGui::TextWrapped("%s", snapshot.proposal->text.c_str());
    if (ImGui::Button("Accept edit")) {
      accept_assistant_edit();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reject edit")) {
      assistant_->reject();
    }
  }
  if (context_preview_open_) {
    ImGui::OpenPopup("What Will Be Sent");
    context_preview_open_ = false;
  }
  if (begin_dialog("What Will Be Sent", ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Structured graph context: %zu bytes", context_preview_.size());
    if (!context_omissions_.empty()) {
      ImGui::TextWrapped("Omissions: %s", context_omissions_.front().c_str());
    }
    ImGui::InputTextMultiline("##context-preview", &context_preview_,
                              ImVec2(720.0F, 420.0F),
                              ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::execute_panel_command(std::string_view command) {
  if (command == "review.refresh") {
    if (review_) {
      const auto snapshot = review_->snapshot();
      if (snapshot.review.selected) {
        review_->request_refresh();
      } else {
        review_->request_list();
      }
    }
  } else if (command == "review.next-hunk") {
    navigate_hunk(true);
  } else if (command == "review.previous-hunk") {
    navigate_hunk(false);
  } else if (command == "review.next-comment") {
    navigate_comment();
  } else if (command == "review.new-comment") {
    begin_review_comment();
  } else if (command == "review.approve") {
    if (review_) {
      review_->request_approve();
    }
  } else if (command == "build.start") {
    request_build();
  } else if (command == "build.test") {
    request_tests();
  } else if (command == "build.push") {
    request_push();
  } else if (command == "assistant.ask") {
    request_assistant_context(AssistantAction::ask);
  } else if (command == "assistant.explain") {
    request_assistant_context(AssistantAction::explain_diagnostic);
  } else if (command == "assistant.propose") {
    request_assistant_context(AssistantAction::propose_edit);
  } else if (command == "assistant.accept") {
    accept_assistant_edit();
  } else if (command == "assistant.reject") {
    if (assistant_) {
      assistant_->reject();
    }
  } else if (command == "assistant.toggle") {
    toggle_assistant();
  } else if (command == "assistant.context") {
    request_assistant_context(std::nullopt);
  } else if (command == "structural.expand") {
    expand_structural_selection();
  } else if (command == "structural.shrink") {
    shrink_structural_selection();
  } else if (command == "structural.definition") {
    select_enclosing_definition();
  } else if (command == "structural.references") {
    select_symbol_references();
  } else if (command == "structural.rename") {
    open_rename();
  } else if (command == "structural.extract") {
    open_extract_function();
  }
}

void App::draw_command_palette() {
  command_palette_visible_ = false;
  if (command_palette_open_) {
    ImGui::OpenPopup("Command Palette");
    command_palette_open_ = false;
  }
  ImGui::SetNextWindowSize(ImVec2(760.0F, 520.0F), ImGuiCond_Appearing);
  if (!begin_dialog("Command Palette", ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }
  command_palette_visible_ = true;
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(-1.0F);
  ImGui::InputTextWithHint("##command-search", "Search commands",
                           &command_palette_query_);
  if (command_palette_query_ != command_palette_previous_query_) {
    command_palette_previous_query_ = command_palette_query_;
    command_palette_selection_ = 0;
  }
  if (!commands_) {
    ImGui::EndPopup();
    return;
  }
  const auto matches = commands_->search(command_palette_query_);
  auto pending = std::optional<std::string>{};
  if (ImGui::BeginTable("command-results", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0.0F, -1.0F))) {
    ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch,
                            0.60F);
    ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthStretch, 0.20F);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.20F);
    table_headers(effective_palette(applied_dark_theme_, applied_high_contrast_),
                  frame_layout_, "palette");
    for (const auto &match : matches) {
      const auto *definition = commands_->find(match.name);
      if (definition == nullptr) {
        continue;
      }
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const auto label = definition->title + "##" + definition->name;
      if (ImGui::Selectable(label.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        pending = definition->name;
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::TextDisabled("%s", definition->group.c_str());
      ImGui::TableSetColumnIndex(2);
      const auto binding = commands_->binding_label(definition->name);
      ImGui::TextDisabled("%s", binding.c_str());
    }
    ImGui::EndTable();
  }
  if (pending) {
    invoke_command(*pending);
    ImGui::CloseCurrentPopup();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  if (command_palette_query_.empty()) {
    ImGui::TextDisabled("Recent commands: %zu", commands_->recent().size());
  }
  for (const auto &diagnostic : command_diagnostics_) {
    ImGui::TextDisabled("%s", diagnostic.c_str());
  }
  ImGui::EndPopup();
}

void App::draw_settings() {
  if (settings_open_) {
    ImGui::OpenPopup("Settings");
    settings_open_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(900.0F, 650.0F), ImGuiCond_Appearing);
  if (!begin_dialog("Settings", ImGuiWindowFlags_NoSavedSettings)) {
    settings_popup_visible_ = false;
    return;
  }
  settings_popup_visible_ = true;

  ImGui::InputTextWithHint("##settings-search", "Search settings",
                           &settings_query_);
  ImGui::SameLine();
  auto repository_scope = settings_scope_ == SettingsScope::repository;
  if (!options_.repository_path) {
    ImGui::BeginDisabled();
  }
  if (checkbox("Repository overrides", &repository_scope)) {
    settings_scope_ =
        repository_scope ? SettingsScope::repository : SettingsScope::user;
  }
  if (!options_.repository_path) {
    ImGui::EndDisabled();
  }
  ImGui::TextDisabled(
      "Editing %s",
      settings_scope_ == SettingsScope::repository
          ? repository_settings_path(*options_.repository_path).string().c_str()
          : user_settings_path().string().c_str());
  for (const auto &diagnostic : settings_diagnostics_) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg));
    ImGui::TextWrapped("%s", diagnostic.message.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::Separator();
  ImGui::BeginChild("settings-scroll", ImVec2(0.0F, -36.0F),
                    ImGuiChildFlags_Borders);

  const auto begin_group = [this](SettingsGroup group) {
    ImGui::PushID(static_cast<int>(group));
    if (!settings_group_matches(group, settings_query_)) {
      return false;
    }
    const auto collapsed_for_proof =
        screenshot_mode() &&
        (group == SettingsGroup::appearance || group == SettingsGroup::editor ||
         group == SettingsGroup::keymap);
    const auto flags = collapsed_for_proof ? ImGuiTreeNodeFlags_None
                                           : ImGuiTreeNodeFlags_DefaultOpen;
    const auto open =
        ImGui::CollapsingHeader(settings_group_name(group).data(), flags);
    if (open) {
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52.0F);
      if (ImGui::SmallButton("Reset")) {
        reset_settings(group);
      }
    }
    return open;
  };
  const auto end_group = [] {
    ImGui::Spacing();
    ImGui::PopID();
  };

  if (begin_group(SettingsGroup::appearance)) {
    auto theme = static_cast<int>(settings_.theme);
    constexpr auto themes =
        std::array{"Follow operating system", "Light", "Dark"};
    field_label("Theme");
    if (ImGui::Combo("##theme", &theme, themes.data(),
                     static_cast<int>(themes.size()))) {
      settings_.theme = static_cast<ThemePreference>(theme);
      apply_settings_live();
      persist_settings();
    }
    ImGui::TextDisabled("UI font");
    ImGui::SameLine(150.0F);
    ImGui::TextUnformatted(settings_.fonts.ui_family.c_str());
    auto ui_size = static_cast<int>(settings_.fonts.ui_size);
    if (slider_int("UI size", &ui_size, 11, 32)) {
      settings_.fonts.ui_size = static_cast<std::uint32_t>(ui_size);
      apply_settings_live(true);
      persist_settings();
    }
    ImGui::TextDisabled("Editor font");
    ImGui::SameLine(150.0F);
    ImGui::TextUnformatted(settings_.fonts.editor_family.c_str());
    auto editor_size = static_cast<int>(settings_.fonts.editor_size);
    if (slider_int("Editor size", &editor_size, 12, 16)) {
      settings_.fonts.editor_size = static_cast<std::uint32_t>(editor_size);
      apply_settings_live(true);
      persist_settings();
    }
  }
  end_group();

  if (begin_group(SettingsGroup::editor)) {
    auto changed = false;
    changed |= checkbox("Wrap long lines", &settings_.editor.wrap);
    changed |= checkbox("Show whitespace", &settings_.editor.whitespace);
    changed |= checkbox("Format .liquid files on save",
                               &settings_.editor.format_on_save);
    if (changed) {
      apply_settings_live();
      persist_settings();
    }
  }
  end_group();

  if (begin_group(SettingsGroup::keymap)) {
    ImGui::TextDisabled("Key bindings for these commands; the full keymap is in "
                        "your .liquid settings file.");
    constexpr auto commands = std::array{
        std::pair{"settings.open", "Cmd+,"},
        std::pair{"build.run", "F7"},
        std::pair{"tests.run", "F8"},
        std::pair{"push.run", "F9"},
    };
    for (const auto &[command, default_binding] : commands) {
      auto &binding = settings_.keymap[command];
      if (binding.empty()) {
        binding = default_binding;
      }
      ImGui::PushID(command);
      const auto *definition = commands_ ? commands_->find(command) : nullptr;
      const auto title = definition != nullptr ? definition->title
                                               : std::string(command);
      if (text_field(title.c_str(), &binding)) {
        persist_settings();
      }
      ImGui::PopID();
    }
  }
  end_group();

  if (begin_group(SettingsGroup::ai)) {
    if (checkbox("Enable AI surface", &settings_.ai.enabled)) {
      apply_settings_live();
      persist_settings();
    }
    ImGui::Text("Provider: %s", settings_.ai.provider.c_str());
    if (text_field("Model", &settings_.ai.model)) {
      apply_settings_live();
      persist_settings();
    }
    const auto key =
        screenshot_mode() ? ConfiguredSecret{true, {}} : anthropic_api_key();
    ImGui::Text("Provider token: %s",
                key.configured ? "******** (configured)" : "not configured");
    if (!screenshot_mode()) {
      ImGui::SetNextItemWidth(280.0F);
      ImGui::InputTextWithHint("##provider-token", "New provider token",
                               &provider_token_input_,
                               ImGuiInputTextFlags_Password);
      ImGui::SameLine();
      if (ImGui::Button("Store token") && !provider_token_input_.empty()) {
        const auto stored = store_anthropic_api_key(provider_token_input_);
        std::ranges::fill(provider_token_input_, '\0');
        provider_token_input_.clear();
        if (!stored) {
          editor_status_ = stored.error().message;
        } else {
          editor_status_ = "provider credential stored";
          apply_settings_live();
        }
      }
      ImGui::TextDisabled(
          "Tokens are read from the OS credential store or environment.");
    }
  }
  end_group();

  if (begin_group(SettingsGroup::lake)) {
    auto remove = std::optional<std::size_t>{};
    for (std::size_t index = 0; index < settings_.lake_accounts.size();
         ++index) {
      auto &account = settings_.lake_accounts[index];
      ImGui::PushID(static_cast<int>(index));
      ImGui::Text("%s", account.name.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove")) {
        remove = index;
      }
      if (screenshot_mode()) {
        ImGui::TextDisabled("%s", account.url.c_str());
      } else {
        if (text_field("URL", &account.url)) {
          persist_settings();
        }
      }
      const auto token = screenshot_mode() ? ConfiguredSecret{true, {}}
                                           : lake_account_token(account.name);
      ImGui::Text("Token: %s", token.configured ? "******** (configured)"
                                                : "not configured");
      if (!screenshot_mode()) {
        ImGui::SetNextItemWidth(280.0F);
        ImGui::InputTextWithHint("##lake-token", "New lake token",
                                 &lake_token_input_,
                                 ImGuiInputTextFlags_Password);
        ImGui::SameLine();
        if (ImGui::Button("Store token") && !lake_token_input_.empty()) {
          const auto stored =
              store_lake_account_token(account.name, lake_token_input_);
          std::ranges::fill(lake_token_input_, '\0');
          lake_token_input_.clear();
          editor_status_ =
              stored ? "lake credential stored" : stored.error().message;
        }
      }
      ImGui::PopID();
    }
    if (remove) {
      settings_.lake_accounts.erase(settings_.lake_accounts.begin() +
                                    static_cast<std::ptrdiff_t>(*remove));
      persist_settings();
    }
    if (!screenshot_mode()) {
      ImGui::SetNextItemWidth(180.0F);
      ImGui::InputTextWithHint("##lake-name", "Account name",
                               &lake_account_name_);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(360.0F);
      ImGui::InputTextWithHint("##lake-url", "https://lake.example",
                               &lake_account_url_);
      ImGui::SameLine();
      if (ImGui::Button("Add") && !lake_account_name_.empty() &&
          !lake_account_url_.empty()) {
        settings_.lake_accounts.push_back(
            {lake_account_name_, lake_account_url_});
        lake_account_name_.clear();
        lake_account_url_.clear();
        persist_settings();
      }
    }
  }
  end_group();

  if (begin_group(SettingsGroup::telemetry)) {
    if (checkbox("Enable telemetry", &settings_.telemetry)) {
      persist_settings();
    }
    ImGui::TextDisabled(
        "Off by default. There are no other telemetry switches.");
  }
  end_group();

  ImGui::EndChild();
  if (ImGui::Button("Close")) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::draw_structural_editing() {
  const auto *screenshot =
      options_.screenshot_state
          ? find_screenshot_state(*options_.screenshot_state)
          : nullptr;
  if (screenshot != nullptr &&
      screenshot->view == ScreenshotView::structural_editing) {
    const auto center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(420.0F, 210.0F), ImGuiCond_Always);
    if (ImGui::Begin("Structural edit preview", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::TextUnformatted("Rename symbol: render_shell to draw_shell");
      ImGui::Separator();
      ImGui::TextUnformatted("src/app.cpp");
      ImGui::BulletText("replace function_definition bytes 3812..4059");
      ImGui::TextDisabled("1 file selected · fingerprint verified");
      ImGui::Separator();
      ImGui::Button("Accept selected file");
      ImGui::SameLine();
      ImGui::Button("Dismiss");
    }
    ImGui::End();
    return;
  }
  if (rename_open_) {
    ImGui::OpenPopup("Rename Symbol");
    rename_open_ = false;
  }
  if (begin_dialog("Rename Symbol", ImGuiWindowFlags_AlwaysAutoResize)) {
    text_field("New name", &structural_name_);
    if (ImGui::Button("Preview") && editor_graph_ && editor_) {
      const auto byte = editor_->byte_offset(editor_->cursor());
      if (byte <= std::numeric_limits<std::uint32_t>::max()) {
        editor_graph_->request_rename(
            std::filesystem::path(editor_->display_path()),
            static_cast<std::uint32_t>(byte), structural_name_);
        editor_status_ = "building rename preview";
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (extract_function_open_) {
    ImGui::OpenPopup("Extract Function");
    extract_function_open_ = false;
  }
  if (begin_dialog("Extract Function", ImGuiWindowFlags_AlwaysAutoResize)) {
    text_field("Function name", &structural_name_);
    if (ImGui::Button("Preview") && editor_graph_ && editor_) {
      if (const auto selection = editor_->selection()) {
        const auto range = SourceRange{
            static_cast<std::uint32_t>(editor_->byte_offset(selection->begin)),
            static_cast<std::uint32_t>(editor_->byte_offset(selection->end)),
            static_cast<std::uint32_t>(selection->begin.line),
            static_cast<std::uint32_t>(selection->begin.byte),
            static_cast<std::uint32_t>(selection->end.line),
            static_cast<std::uint32_t>(selection->end.byte)};
        editor_graph_->request_extract_function(
            std::filesystem::path(editor_->display_path()), range,
            structural_name_);
        editor_status_ = "building extract-function preview";
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::draw_structural_panel() {
  if (structural_references_.empty() && !structural_preview_open_) {
    ImGui::TextWrapped(
        "%s",
        panels_.descriptor(PanelId::structural_editing).empty_state.data());
    return;
  }
  if (!structural_references_.empty()) {
    ImGui::Text("References (%zu)", structural_references_.size());
    for (std::size_t index = 0; index < structural_references_.size();
         ++index) {
      const auto &reference = structural_references_[index];
      const auto label = reference.path.generic_string() + ":" +
                         std::to_string(reference.range.start_line + 1) +
                         "##ref-" + std::to_string(index);
      if (ImGui::Selectable(label.c_str())) {
        const auto path = reference.path.generic_string();
        if (!editor_ || editor_->display_path() != path) {
          open_editor(path, "structural:" + path);
        }
        editor_->set_cursor(
            {reference.range.start_line, reference.range.start_column});
        editor_->set_cursor(
            {reference.range.end_line, reference.range.end_column}, true);
        ensure_cursor_visible();
      }
    }
  }
  if (!structural_preview_open_) {
    return;
  }
  for (const auto &diagnostic : structural_diagnostics_) {
    ImGui::TextWrapped("Could not prepare this edit: %s", diagnostic.c_str());
  }
  for (const auto &unresolved : structural_unresolved_) {
    ImGui::TextWrapped("This text occurrence could not be resolved: %s",
                       unresolved.c_str());
  }
  auto files = std::vector<std::string>{};
  for (const auto &edit : structural_preview_) {
    const auto path = edit.target.path.generic_string();
    if (std::ranges::find(files, path) == files.end()) {
      files.push_back(path);
    }
  }
  for (const auto &path : files) {
    auto accepted = std::ranges::find(structural_accepted_files_, path) !=
                    structural_accepted_files_.end();
    const auto label = path + "##structural-file";
    if (checkbox(label.c_str(), &accepted)) {
      const auto found = std::ranges::find(structural_accepted_files_, path);
      if (accepted && found == structural_accepted_files_.end()) {
        structural_accepted_files_.push_back(path);
      } else if (!accepted && found != structural_accepted_files_.end()) {
        structural_accepted_files_.erase(found);
      }
    }
    for (const auto &edit : structural_preview_) {
      if (edit.target.path.generic_string() != path) {
        continue;
      }
      ImGui::BulletText("%s %s bytes %u..%u",
                        structural_edit_operation_name(edit.operation).data(),
                        edit.target.node.kind.c_str(),
                        edit.target.node.range.start_byte,
                        edit.target.node.range.end_byte);
    }
  }
  if (!structural_preview_.empty()) {
    if (ImGui::Button("Accept selected files")) {
      apply_structural_preview(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Accept all")) {
      apply_structural_preview(true);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Dismiss")) {
    structural_preview_open_ = false;
    structural_preview_.clear();
  }
}

void App::invoke_plugin_command(std::string_view name) {
  if (!plugin_host_) {
    return;
  }
  auto invoked = plugin_host_->invoke_command(name, plugin_empty_map_cbor());
  if (!invoked) {
    editor_status_ = invoked.error().message;
    if (!invoked.error().context.empty()) {
      editor_status_ += ": " + invoked.error().context;
    }
    return;
  }
  editor_status_ = "plugin command completed: " + std::string(name);
}

void App::poll_plugins() {
  const auto now = std::chrono::steady_clock::now();
  if (!plugin_host_ || now < next_plugin_refresh_) {
    return;
  }
  plugin_panels_ = plugin_host_->panels(plugin_null_cbor());
  next_plugin_refresh_ = now + std::chrono::milliseconds(500);
}

void App::sync_panel_system() {
  const auto graph =
      editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
  const auto review_snapshot =
      review_ ? review_->snapshot() : ReviewControllerSnapshot{};
  const auto completed_actions = std::ranges::count_if(
      graph.build_actions, [](const BuildActionSnapshot &action) {
        return action.state == BuildActionState::hit ||
               action.state == BuildActionState::done ||
               action.state == BuildActionState::failed;
      });
  if (graph.build_running) {
    if (!build_was_running_) {
      panels_.begin_operation("build", PanelId::build, "Building repository");
    }
    panels_.update_operation("build", "Building repository", completed_actions,
                             graph.build_actions.size());
  } else if (build_was_running_) {
    const auto failed =
        graph.build_result.starts_with("failed") || !graph.query_error.empty();
    panels_.finish_operation("build",
                             failed ? PanelNotificationKind::error
                                    : PanelNotificationKind::success,
                             failed ? "Build did not finish successfully"
                                    : "Build finished successfully");
  }
  build_was_running_ = graph.build_running;

  if (graph.deploy.running) {
    if (!sync_was_running_) {
      panels_.begin_operation("sync", PanelId::build, "Syncing saved drips");
    }
    panels_.update_operation("sync", "Syncing saved drips");
  } else if (sync_was_running_) {
    const auto failed = !graph.deploy.error.empty();
    panels_.finish_operation(
        "sync",
        failed ? PanelNotificationKind::error : PanelNotificationKind::success,
        failed ? "Sync did not finish successfully" : "Saved drips are synced");
  }
  sync_was_running_ = graph.deploy.running;

  if (repository_) {
    repository_->poll();
  }
  const auto loading =
      repository_ ? repository_->loading_directory_count() : 0U;
  if (loading > 0) {
    if (!hydration_was_running_) {
      panels_.begin_operation("hydration", PanelId::repository,
                              "Loading repository folders");
    }
    panels_.update_operation("hydration", "Loading repository folders");
  } else if (hydration_was_running_) {
    const auto failed = repository_ && repository_->directory_error_count() > 0;
    panels_.finish_operation(
        "hydration",
        failed ? PanelNotificationKind::error : PanelNotificationKind::success,
        failed ? "Some repository folders could not be loaded"
               : "Repository folders are ready");
  }
  hydration_was_running_ = loading > 0;

  const auto assistant_snapshot =
      assistant_ ? assistant_->snapshot() : AssistantSnapshot{};
  if (assistant_snapshot.running) {
    if (!assistant_was_running_) {
      panels_.begin_operation("assistant", PanelId::assistant,
                              "Assistant is responding", true);
    }
    panels_.update_operation("assistant", "Assistant is responding");
  } else if (assistant_was_running_) {
    const auto failed = assistant_snapshot.status.starts_with("error:");
    panels_.finish_operation("assistant",
                             failed ? PanelNotificationKind::error
                                    : PanelNotificationKind::information,
                             failed ? "Assistant response failed"
                                    : "Assistant response stopped");
  }
  assistant_was_running_ = assistant_snapshot.running;

  panels_.set_panel_state(
      PanelId::repository,
      {.message = repository_ ? "Repository open" : "No repository open",
       .error = repository_ ? std::string(repository_->error()) : std::string{},
       .busy = loading > 0});
  panels_.set_panel_state(
      PanelId::search,
      {.message = repository_ ? "Ready to search" : "No repository open"});
  panels_.set_panel_state(PanelId::review,
                          {.message = review_snapshot.review.selected
                                          ? "Review open"
                                          : "No review open",
                           .error = review_snapshot.error,
                           .busy = review_snapshot.busy});
  panels_.set_panel_state(
      PanelId::build, {.message = graph.build_result,
                       .error = graph.deploy.error.empty() ? graph.query_error
                                                           : graph.deploy.error,
                       .busy = graph.build_running || graph.deploy.running});
  panels_.set_panel_state(PanelId::multiplayer,
                          {.message = "No active repository session"});
  panels_.set_panel_state(
      PanelId::diagnostics,
      {.message = std::to_string(graph.diagnostics.size() +
                                 graph.build_diagnostics.size()) +
                  " diagnostics",
       .error = graph.query_error,
       .busy = false});
  panels_.set_panel_state(
      PanelId::assistant,
      {.message = assistant_snapshot.status,
       .error = assistant_snapshot.status.starts_with("error:")
                    ? assistant_snapshot.status
                    : std::string{},
       .busy = assistant_snapshot.running});
  auto plugin_error = std::string{};
  if (!plugin_failures_.empty()) {
    plugin_error = plugin_failures_.front().error;
  } else {
    const auto failed_panel = std::ranges::find_if(
        plugin_panels_,
        [](const PluginPanelSnapshot &panel) { return !panel.error.empty(); });
    if (failed_panel != plugin_panels_.end()) {
      plugin_error = failed_panel->error;
    }
  }
  panels_.set_panel_state(
      PanelId::plugins,
      {.message = std::to_string(plugin_panels_.size()) + " plugin panels" +
                  (plugin_untrusted_.empty()
                       ? std::string{}
                       : ", " + std::to_string(plugin_untrusted_.size()) +
                             " not trusted"),
       .error = std::move(plugin_error)});
  panels_.set_panel_state(PanelId::structural_editing,
                          {.message = structural_preview_open_
                                          ? "Edit preview ready"
                                          : "No edit preview",
                           .error = structural_diagnostics_.empty()
                                        ? std::string{}
                                        : structural_diagnostics_.front()});

  for (const auto &request : panels_.take_cancel_requests()) {
    if (request == "assistant" && assistant_) {
      assistant_->cancel();
    }
  }
}

void App::refresh_multiplayer() {
  if (!editor_ || !options_.repository_path || multiplayer_refresh_.valid()) {
    return;
  }
  const auto cursor = editor_->cursor();
  const auto byte = editor_->byte_offset(cursor);
  const auto range = liquid::frontend_source_range{
      .start = {.byte = byte,
                .line = cursor.line + 1,
                .column = cursor.byte + 1},
      .end = {
          .byte = byte, .line = cursor.line + 1, .column = cursor.byte + 1}};
  request_repaint();
  multiplayer_refresh_ = std::async(
      std::launch::async, [repository_path = *options_.repository_path,
                           path = std::string(editor_->display_path()),
                           session = multiplayer_session_, range] {
        return liquify::refresh_multiplayer(repository_path, path, session,
                                            range);
      });
}

void App::poll_multiplayer() {
  if (!options_.repository_path || !editor_) {
    return;
  }
  if (multiplayer_refresh_.valid() &&
      multiplayer_refresh_.wait_for(std::chrono::seconds{0}) ==
          std::future_status::ready) {
    auto refreshed = multiplayer_refresh_.get();
    if (!refreshed) {
      editor_status_ = refreshed.error().message;
    } else {
      multiplayer_ = std::move(*refreshed);
      if (plugin_host_) {
        for (const auto &value : multiplayer_->presence) {
          plugin_host_->publish_presence(value);
        }
        for (const auto &value : multiplayer_->leases) {
          plugin_host_->publish_lease(value);
        }
        plugin_host_->publish_attribution(editor_->display_path(),
                                          multiplayer_->change,
                                          multiplayer_->attribution);
      }
    }
  }
  const auto now = std::chrono::steady_clock::now();
  if (now >= next_multiplayer_refresh_) {
    refresh_multiplayer();
    next_multiplayer_refresh_ = now + std::chrono::seconds{1};
  }
}

void App::take_editor_lease() {
  if (!editor_ || !options_.repository_path) {
    return;
  }
  auto claim = liquid::lease_claim{};
  claim.path = std::string(editor_->display_path());
  claim.kind = liquid::lease_kind::file;
  auto acquired = acquire_multiplayer_lease(*options_.repository_path,
                                            multiplayer_session_, claim);
  if (!acquired) {
    editor_status_ = acquired.error().message;
    return;
  }
  editor_status_ = "lease acquired";
  next_multiplayer_refresh_ = std::chrono::steady_clock::time_point{};
}

void App::release_editor_lease() {
  if (!editor_ || !options_.repository_path) {
    return;
  }
  auto claim = liquid::lease_claim{};
  claim.path = std::string(editor_->display_path());
  claim.kind = liquid::lease_kind::file;
  auto released = release_multiplayer_lease(*options_.repository_path,
                                            multiplayer_session_, claim);
  if (!released) {
    editor_status_ = released.error().message;
    return;
  }
  editor_status_ = "lease released";
  next_multiplayer_refresh_ = std::chrono::steady_clock::time_point{};
}

bool App::editor_mutation_allowed() const {
  if (!editor_ || !multiplayer_) {
    return true;
  }
  const auto path = editor_->display_path();
  const auto line = static_cast<std::uint64_t>(editor_->cursor().line + 1);
  for (const auto &lease : multiplayer_->leases) {
    if (lease.session == multiplayer_session_ || lease.claim.path != path) {
      continue;
    }
    if (lease.claim.kind == liquid::lease_kind::file || !lease.claim.range ||
        (lease.claim.range->start.line <= line &&
         line <= lease.claim.range->end.line)) {
      return false;
    }
  }
  return true;
}

void App::draw_plugin_panels() {
  if (plugin_failures_.empty() && plugin_untrusted_.empty() &&
      plugin_panels_.empty()) {
    return;
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Plugins");
  // A repository-named plugin that the user has not trusted is listed and
  // never loaded. Trust is granted outside the IDE, so nothing the repository
  // contains can grant it.
  for (const auto &untrusted : plugin_untrusted_) {
    ImGui::TextColored(ImVec4(0.80F, 0.55F, 0.10F, 1.0F),
                       "%s: not trusted (%s)",
                       untrusted.path.generic_string().c_str(),
                       untrusted.digest.c_str());
    ImGui::TextWrapped("Named by %s. To load it: liquify plugin trust \"%s\"",
                       untrusted.repository.generic_string().c_str(),
                       untrusted.path.generic_string().c_str());
  }
  for (const auto &failure : plugin_failures_) {
    const auto path = failure.path.empty() ? std::string("configuration")
                                           : failure.path.generic_string();
    ImGui::TextColored(ImVec4(0.75F, 0.20F, 0.15F, 1.0F), "%s: %s",
                       path.c_str(), failure.error.c_str());
  }
  for (const auto &panel : plugin_panels_) {
    const auto panel_name = panel.plugin + "/" + panel.name;
    if (!panel.error.empty()) {
      ImGui::TextColored(ImVec4(0.75F, 0.20F, 0.15F, 1.0F), "%s: %s",
                         panel_name.c_str(), panel.error.c_str());
      continue;
    }
    auto model = decode_plugin_text_panel(panel.view_model_cbor);
    if (!model) {
      ImGui::TextColored(ImVec4(0.75F, 0.20F, 0.15F, 1.0F), "%s: %s",
                         panel_name.c_str(), model.error().message.c_str());
      continue;
    }
    ImGui::Text("%s: %s", panel_name.c_str(), model->title.c_str());
    ImGui::TextWrapped("%s", model->text.c_str());
  }
}

void App::draw_editor_modes() {
  const auto mode = editing_mode_name(editor_->mode());
  if (ImGui::Button(mode.data())) {
    ImGui::OpenPopup("editor-mode");
  }
  if (ImGui::BeginPopup("editor-mode")) {
    for (const auto value : {EditingMode::standard, EditingMode::modal_command,
                             EditingMode::modal_insert}) {
      if (ImGui::MenuItem(editing_mode_name(value).data(), nullptr,
                          editor_->mode() == value)) {
        editor_->set_mode(value);
        editor_focused_ = true;
      }
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!analysis_buffer_open_);
  if (ImGui::Button("Structural")) {
    ImGui::OpenPopup("structural-actions");
  }
  if (ImGui::BeginPopup("structural-actions")) {
    if (ImGui::MenuItem("Expand selection")) {
      expand_structural_selection();
    }
    if (ImGui::MenuItem("Shrink selection")) {
      shrink_structural_selection();
    }
    if (ImGui::MenuItem("Select enclosing definition")) {
      select_enclosing_definition();
    }
    if (ImGui::MenuItem("Rename")) {
      open_rename();
    }
    if (ImGui::MenuItem("Extract Function")) {
      open_extract_function();
    }
    ImGui::EndPopup();
  }
  ImGui::EndDisabled();
  if (assistant_) {
    ImGui::SameLine();
    if (ImGui::Button(assistant_->snapshot().enabled ? "AI: on" : "AI: off")) {
      toggle_assistant();
    }
  }
}

void App::draw_editor_toolbar() {
  // One compact row (liquify#101): Save is the surface's primary action; the rest are
  // secondary buttons in groups divided by hairlines, each explained by a tooltip.
  const auto origin = ImGui::GetCursorScreenPos();
  const auto available = ImGui::GetContentRegionAvail();
  const auto palette =
      effective_palette(applied_dark_theme_, applied_high_contrast_);
  const auto divider = [&] {
    ImGui::SameLine(0.0F, design::space3);
    const auto at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(at.x, at.y + design::space1),
        ImVec2(at.x, at.y + ImGui::GetFrameHeight() - design::space1),
        packed_color(palette.border_subtle), 1.0F);
    ImGui::Dummy(ImVec2(1.0F, ImGui::GetFrameHeight()));
    ImGui::SameLine(0.0F, design::space3);
  };
  const auto tooltip = [](const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::SetTooltip("%s", text);
    }
  };
  ImGui::BeginGroup();
  const auto first_vertex = ImGui::GetWindowDrawList()->VtxBuffer.Size;
  ImGui::BeginDisabled(review_diff_open_);
  if (kit_button("Save", ButtonKind::primary)) {
    save_editor();
    editor_focused_ = true;
  }
  frame_layout_.toolbar_text = text_geometry(first_vertex);
  tooltip("Save this file as a drip (Primary+S)");
  ImGui::EndDisabled();
  divider();
  if (kit_button("Build (F7)", ButtonKind::secondary)) {
    request_build();
  }
  tooltip("Build the repository with its recorded command");
  ImGui::SameLine();
  if (kit_button("Tests (F8)", ButtonKind::secondary)) {
    request_tests();
  }
  tooltip("Run every test the build defines");
  ImGui::SameLine();
  if (kit_button("Push (F9)", ButtonKind::secondary)) {
    request_push();
  }
  tooltip("Push your changes to lake");
  divider();
  if (kit_button(shell_state_.editor_split ? "Unsplit" : "Split",
                 ButtonKind::quiet)) {
    shell_state_.editor_split = !shell_state_.editor_split;
    persist_shell_state();
  }
  tooltip("Show a second view of this file");
  ImGui::SameLine();
  if (kit_button(shell_state_.bottom_panel_visible ? "Hide panel"
                                                   : "Show panel",
                 ButtonKind::quiet)) {
    shell_state_.bottom_panel_visible = !shell_state_.bottom_panel_visible;
    persist_shell_state();
  }
  tooltip("Show or hide the bottom panel (Primary+J)");
  divider();
  ImGui::BeginDisabled(review_diff_open_);
  draw_editor_modes();
  ImGui::EndDisabled();
  ImGui::EndGroup();
  ImGui::Separator();
  const auto end = ImGui::GetItemRectMax();
  frame_layout_.toolbar = {origin.x, origin.y, available.x, end.y - origin.y};
}

void App::draw_multiplayer_details() {
  if (!multiplayer_) {
    ImGui::TextDisabled("Open a repository file to inspect collaboration.");
    return;
  }
  auto count = std::size_t{};
  for (const auto &value : multiplayer_->presence) {
    if (value.session == multiplayer_session_) {
      continue;
    }
    ImGui::PushID(static_cast<int>(count++));
    detail_text(multiplayer_presence_text(value));
    identifier_detail("Actor", value.actor.taap_id);
    identifier_detail("Session", liquid::to_string(value.session));
    ImGui::Separator();
    ImGui::PopID();
  }
  if (count == 0) {
    ImGui::TextDisabled("No other active sessions");
  }
  if (ImGui::CollapsingHeader("Leases")) {
    for (const auto &value : multiplayer_->leases) {
      detail_text(multiplayer_lease_text(value));
    }
  }
  if (ImGui::CollapsingHeader("Local identity")) {
    identifier_detail("Session", liquid::to_string(multiplayer_session_));
    identifier_detail("Change", liquid::to_string(multiplayer_->change.value));
  }
}

void App::draw_line_attribution(std::size_t line) {
  ImGui::Text("Line %zu attribution", line + 1);
  if (multiplayer_) {
    const auto found = std::ranges::find(multiplayer_->attribution, line + 1,
                                         &liquid::line_attribution::line);
    if (found != multiplayer_->attribution.end()) {
      identifier_detail("Author", found->by.taap_id);
      identifier_detail("Session", liquid::to_string(found->session));
      identifier_detail("Change", liquid::to_string(found->change.value));
      if (found->agent) {
        ImGui::TextDisabled("Assisted edit");
      }
      return;
    }
  }
  const auto graph =
      editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
  const auto found =
      std::ranges::find(graph.attribution, line, &AttributionSnapshot::line);
  if (found != graph.attribution.end()) {
    identifier_detail("Author", found->actor);
    // The graph's change is already abbreviated; the repository snapshot above
    // owns the full value.
    ImGui::TextDisabled("Change: %s",
                        compact_identifier(found->change).c_str());
  } else {
    ImGui::TextDisabled("No saved attribution for this line");
  }
}

void App::draw_repository_details(const EditorGraphSnapshot &snapshot) {
  if (!repository_details_open_) {
    return;
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Repository details");
  if (repository_) {
    ImGui::TextWrapped("%s", options_.repository_path->string().c_str());
    ImGui::Text("Opened in %.3f ms", repository_->open_time().count());
    identifier_detail("Working change",
                      std::string(repository_->current_change()));
  }
  if (first_run_.session()) {
    identifier_detail("Identity", first_run_.session()->actor);
  }
  identifier_detail("Session", liquid::to_string(multiplayer_session_));
  detail_text(editor_graph_status(snapshot));
  if (snapshot.daemon && snapshot.daemon->last_drip) {
    identifier_detail("Last drip",
                      liquid::to_string(snapshot.daemon->last_drip->value));
  }
  ImGui::Text("Edit to first result %.3f ms", snapshot.edit_to_diagnostic_ms);
  if (review_) {
    const auto review = review_->snapshot();
    ImGui::Text("Text diff %.3f ms | anchor map %.3f ms",
                review.review.open_to_first_hunk_ms,
                review.review.anchor_mapping_ms);
  }
  if (!snapshot.error.empty()) {
    detail_text(snapshot.error);
  }
  if (repository_) {
    const auto state = StatusBarInput{
        .repository_open = true,
        .daemon_running = snapshot.daemon_running,
        .daemon_available = editor_graph_ != nullptr,
        .sync_running = sync_was_running_,
        .hydration_running = hydration_was_running_,
        .build_running = snapshot.build_running,
        .offline = repository_->offline(),
        .pending_uploads = repository_->pending_uploads(),
        .hydration_debt = repository_->hydration_debt(),
        .remote_bytes = repository_->remote_bytes(),
        .presence_count = repository_->presence_count(),
        .notification_count = panels_.notifications().size(),
        .build_result = snapshot.build_result,
        .branch = std::string(repository_->branch()),
    };
    for (const auto &segment : status_bar_segments(state)) {
      if (segment != "Sign in" && segment != "No change") {
        detail_text(segment);
      }
    }
  }
}

auto App::panel_label(PanelId id) const -> std::string {
  auto label = std::string(panels_.descriptor(id).title);
  if (id == PanelId::diagnostics) {
    const auto snapshot =
        editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
    const auto count =
        screenshot_mode()
            ? 3
            : snapshot.diagnostics.size() + snapshot.build_diagnostics.size();
    label += " " + std::to_string(count);
    if (!structural_diagnostics_.empty() || !snapshot.query_error.empty()) {
      label += " !";
    }
  }
  if (panels_.panel_state(id).busy) {
    label += " · Working";
  }
  return label;
}

void App::draw_editor() {
  const auto path = std::string(editor_->display_path());
  const auto graph_snapshot =
      editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
  auto symbol_name = std::optional<std::string_view>{};
  if (const auto *symbol = navigation_symbol_at_cursor()) {
    symbol_name = symbol->name;
  } else if (screenshot_mode()) {
    symbol_name = "greeting";
  }
  const auto path_breadcrumbs = breadcrumbs(path, symbol_name);
  if (breadcrumb_focus_requested_) {
    ImGui::SetKeyboardFocusHere();
    breadcrumb_focus_requested_ = false;
  }
  for (std::size_t index = 0; index < path_breadcrumbs.size(); ++index) {
    const auto &breadcrumb = path_breadcrumbs[index];
    const auto label =
        breadcrumb.label + "##breadcrumb-" + std::to_string(index);
    if (ImGui::SmallButton(label.c_str()) && !breadcrumb.symbol &&
        breadcrumb.path == std::filesystem::path(path)) {
      editor_focused_ = true;
    }
    if (index + 1 < path_breadcrumbs.size()) {
      ImGui::SameLine();
      ImGui::TextDisabled(">");
      ImGui::SameLine();
    }
  }
  if (editor_->dirty()) {
    ImGui::SameLine();
    ImGui::TextUnformatted("*");
  }
  if (review_diff_open_) {
    ImGui::SameLine();
    ImGui::TextDisabled("read-only text diff");
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("...##file-actions")) {
    ImGui::OpenPopup("file-actions");
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("File actions and lease controls");
  }
  if (ImGui::BeginPopup("file-actions")) {
    ImGui::BeginDisabled(review_diff_open_ || !options_.repository_path);
    if (ImGui::MenuItem("Take lease")) {
      take_editor_lease();
    }
    if (ImGui::MenuItem("Release lease")) {
      release_editor_lease();
    }
    ImGui::EndDisabled();
    if (ImGui::MenuItem("Collaboration details")) {
      panels_.focus(PanelId::multiplayer);
      shell_state_.sidebar_visible = true;
      persist_shell_state();
    }
    ImGui::EndPopup();
  }
  draw_editor_toolbar();
  if (!editor_mutation_allowed()) {
    ImGui::TextColored(ImVec4(0.75F, 0.20F, 0.15F, 1.0F),
                       "Read-only: another person holds a lease");
  }
  if (graph_snapshot.hover) {
    detail_text(graph_snapshot.hover->signature);
    if (!graph_snapshot.hover->documentation.empty()) {
      detail_text(graph_snapshot.hover->documentation);
    }
  }
  if (find_open_) {
    ImGui::Separator();
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::InputTextWithHint("##find-buffer", "Find in buffer",
                                 &find_query_)) {
      buffer_matches_ = editor_->find(find_query_, find_options_);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputTextWithHint("##replace-buffer", "Replace", &replace_query_);
    ImGui::SameLine();
    if (checkbox("Regex", &find_options_.regular_expression)) {
      buffer_matches_ = editor_->find(find_query_, find_options_);
    }
    ImGui::SameLine();
    if (checkbox("Case", &find_options_.case_sensitive)) {
      buffer_matches_ = editor_->find(find_query_, find_options_);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu matches", buffer_matches_.size());
    ImGui::SameLine();
    if (ImGui::Button("Next") && !buffer_matches_.empty()) {
      const auto &match =
          buffer_matches_[find_match_index_++ % buffer_matches_.size()];
      editor_->set_cursor(match.range.begin);
      editor_->set_cursor(match.range.end, true);
      ensure_cursor_visible();
    }
    ImGui::SameLine();
    if (ImGui::Button("Replace all") && !review_diff_open_) {
      const auto change =
          editor_->replace_all(find_query_, replace_query_, find_options_);
      if (change) {
        apply_change(change->first_line, change->erased_lines,
                     change->inserted_lines);
        buffer_matches_ = editor_->find(find_query_, find_options_);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      find_open_ = false;
    }
  }

  const auto available = ImGui::GetContentRegionAvail();
  const auto size =
      ImVec2(std::max(available.x, 1.0F), std::max(available.y, 1.0F));
  auto *editor_font = ImGui::GetIO().Fonts->Fonts[editor_font_index_];
  ImGui::PushFont(editor_font);
  const auto position = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("editor-surface", size,
                         ImGuiButtonFlags_MouseButtonLeft);
  const auto hovered = ImGui::IsItemHovered();
  auto *draw_list = ImGui::GetWindowDrawList();
  const auto palette =
      effective_palette(applied_dark_theme_, applied_high_contrast_);
  draw_list->AddRectFilled(position,
                           ImVec2(position.x + size.x, position.y + size.y),
                           packed_color(palette.surface));

  editor_x_ = position.x;
  editor_y_ = position.y;
  editor_width_ = size.x;
  editor_height_ = size.y;
  editor_drop_rectangle_ = {position.x, position.y, size.x, size.y};
  frame_layout_.editor = editor_drop_rectangle_;
  line_height_ = ImGui::GetFontSize() + 4.0F;
  const auto *glyph = ImGui::GetFont()->FindGlyph('M');
  cell_width_ =
      glyph == nullptr ? ImGui::GetFontSize() * 0.6F : glyph->AdvanceX;
  visible_lines_ =
      std::max<std::size_t>(1, static_cast<std::size_t>(size.y / line_height_));
  editor_visible_ = true;
  draw_list->PushClipRect(
      position, ImVec2(position.x + size.x, position.y + size.y), true);
  draw_list->AddCallback(
      [](const ImDrawList *, const ImDrawCmd *command) {
        auto *app = static_cast<App *>(command->UserCallbackData);
        const auto clip = command->ClipRect;
        app->render_editor({clip.x, clip.y, clip.z - clip.x, clip.w - clip.y});
      },
      this);
  draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
  draw_list->PopClipRect();
  const auto item_focused = ImGui::IsItemFocused();
  if (item_focused) {
    editor_focused_ = true;
  } else if (ImGui::IsAnyItemFocused()) {
    editor_focused_ = false;
  }
  if (editor_focused_ || input_.drop_target() == DropTarget::editor) {
    const auto thickness =
        input_.drop_target() == DropTarget::editor ? 3.0F : 2.0F;
    draw_list->AddLine(position, ImVec2(position.x + size.x, position.y),
                       packed_color(palette.focus), thickness);
  }

  if (hovered) {
    const auto wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0F) {
      const auto amount = static_cast<std::size_t>(std::abs(wheel) * 3.0F);
      if (wheel > 0.0F) {
        scroll_line_ = scroll_line_ > amount ? scroll_line_ - amount : 0;
      } else {
        scroll_line_ =
            std::min(scroll_line_ + amount, editor_->line_count() - 1);
      }
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      editor_focused_ = true;
      auto handled_gutter = false;
      const auto clicked_gutter =
          ImGui::GetIO().MousePos.x < editor_x_ + editor_gutter_width;
      if (clicked_gutter) {
        const auto row = static_cast<std::size_t>(
            std::max(0.0F, std::floor((ImGui::GetIO().MousePos.y - editor_y_) /
                                      line_height_)));
        const auto line =
            std::min(scroll_line_ + row, editor_->line_count() - 1);
        const auto diagnostic = std::ranges::find_if(
            graph_snapshot.diagnostics, [&](const auto &item) {
              return item.location.path.generic_string() ==
                         editor_->display_path() &&
                     item.location.range.start_line == line;
            });
        if (ImGui::GetIO().MousePos.x < editor_x_ + 16.0F) {
          attribution_line_ = line;
          ImGui::OpenPopup("line-attribution");
          handled_gutter = true;
        } else if (diagnostic != graph_snapshot.diagnostics.end()) {
          editor_->set_cursor({line, diagnostic->location.range.start_column});
          panels_.focus(PanelId::diagnostics);
          panels_.clear_focus_request();
          shell_state_.bottom_panel_visible = true;
          handled_gutter = true;
        }
      }
      if (!handled_gutter) {
        const auto prior = editor_->cursor();
        position_cursor(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y,
                        ImGui::GetIO().KeyShift);
        if (ImGui::GetIO().KeyAlt && ImGui::GetIO().KeyShift) {
          column_selection_anchor_ = editor_->cursor();
        } else if (ImGui::GetIO().KeyAlt) {
          const auto added = editor_->cursor();
          editor_->set_cursor(prior);
          editor_->add_cursor(added);
        } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          editor_->select(SelectionKind::word);
        }
      }
    } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      position_cursor(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y,
                      true);
      if (column_selection_anchor_) {
        editor_->select_column(*column_selection_anchor_, editor_->cursor());
      }
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      column_selection_anchor_.reset();
    }
    if (ImGui::GetIO().MousePos.x < editor_x_ + editor_gutter_width) {
      const auto row = static_cast<std::size_t>(std::max(
          0.0F,
          std::floor((ImGui::GetIO().MousePos.y - editor_y_) / line_height_)));
      const auto line = std::min(scroll_line_ + row, editor_->line_count() - 1);
      if (!ImGui::IsPopupOpen("line-attribution")) {
        ImGui::BeginTooltip();
        draw_line_attribution(line);
        ImGui::TextDisabled("Click the author marker for copy controls");
        ImGui::EndTooltip();
      }
    }
  }
  if (ImGui::BeginPopup("line-attribution")) {
    if (attribution_line_) {
      draw_line_attribution(*attribution_line_);
    }
    ImGui::EndPopup();
  }
  update_text_input_area();
  draw_composition();
  ImGui::PopFont();
}

void App::draw_diagnostics_panel(const EditorGraphSnapshot &graph_snapshot) {
  checkbox("Repository details", &repository_details_open_);
  draw_repository_details(graph_snapshot);
  for (const auto &diagnostic : structural_diagnostics_) {
    detail_text(diagnostic);
  }
  if (screenshot_mode()) {
    const auto palette =
        effective_palette(applied_dark_theme_, applied_high_contrast_);
    const auto first_vertex = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    ImGui::TextColored(imgui_color(palette.danger),
                       "error: expected ';' after return statement");
    frame_layout_.panels[PanelId::diagnostics].text =
        text_geometry(first_vertex);
    ImGui::TextUnformatted("warning: unused result from save_app_shell_state");
    ImGui::TextDisabled("info: consider adding a return type");
    return;
  }
  auto visible_diagnostics = graph_snapshot.diagnostics;
  visible_diagnostics.insert(visible_diagnostics.end(),
                             graph_snapshot.build_diagnostics.begin(),
                             graph_snapshot.build_diagnostics.end());
  if (!graph_snapshot.query_error.empty()) {
    draw_user_error(describe_query_failure(graph_query_failure(graph_snapshot),
                                           repository_path()),
                    "query-failure");
  }
  for (std::size_t index = 0; index < visible_diagnostics.size(); ++index) {
    const auto &diagnostic = visible_diagnostics[index];
    const auto label =
        std::string(diagnostic_severity_name(diagnostic.severity)) + ": " +
        diagnostic.message + "##diagnostic-" + std::to_string(index);
    if (ImGui::Selectable(label.c_str())) {
      const auto target = diagnostic.location.path.generic_string();
      if (editor_->display_path() != target) {
        open_editor(target, "graph:" + target);
      }
      editor_->set_cursor({diagnostic.location.range.start_line,
                           diagnostic.location.range.start_column});
      ensure_cursor_visible();
    }
  }
  if (!graph_snapshot.completions.empty()) {
    ImGui::Separator();
    ImGui::Text("Completions (%zu)", graph_snapshot.completions.size());
    const auto count =
        std::min<std::size_t>(graph_snapshot.completions.size(), 20);
    for (std::size_t index = 0; index < count; ++index) {
      const auto &completion = graph_snapshot.completions[index];
      const auto display =
          completion.signature.empty() ? completion.name : completion.signature;
      const auto label = display + "##completion-" + std::to_string(index);
      if (ImGui::Selectable(label.c_str())) {
        if (!editor_mutation_allowed()) {
          editor_status_ = "edit refused: another session holds a lease";
          continue;
        }
        const auto change = editor_->insert_text(completion.name);
        if (change) {
          apply_change(change->first_line, change->erased_lines,
                       change->inserted_lines);
        }
        editor_focused_ = true;
      }
    }
  }
}

void App::draw_build_panel(const EditorGraphSnapshot &snapshot) {
  const auto available = ImGui::GetContentRegionAvail();
  ImGui::BeginChild(
      "build-panel",
      ImVec2(std::max(available.x, 1.0F), std::max(available.y, 1.0F)),
      ImGuiChildFlags_Border);
  if (screenshot_mode()) {
    const auto palette =
        effective_palette(applied_dark_theme_, applied_high_contrast_);
    ImGui::SameLine();
    ImGui::TextColored(imgui_color(palette.success), "passed");
    ImGui::TextUnformatted("configure: hit");
    ImGui::TextUnformatted("liquify-core: done");
    ImGui::SameLine();
    ImGui::TextDisabled("worker local-arm64 · critical");
    ImGui::TextUnformatted("liquify: done");
    ImGui::TextDisabled("requires liquify-core");
    ImGui::Separator();
    ImGui::TextUnformatted("Tests 49 passed · 0 failed");
    ImGui::TextDisabled("Push: nothing waiting");
    ImGui::EndChild();
    return;
  }
  if (ImGui::SmallButton("Build repository")) {
    request_build();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Run tests")) {
    request_tests();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Push")) {
    request_push();
  }
  if (snapshot.build_error) {
    draw_user_error(
        describe_build_failure(*snapshot.build_error, repository_path()),
        "build-failure", [this] { request_build(); });
  }
  if (snapshot.build_running) {
    ImGui::SameLine();
    ImGui::TextDisabled("running");
  } else if (!snapshot.build_result.empty()) {
    ImGui::SameLine();
    ImGui::TextUnformatted(snapshot.build_result.c_str());
  }
  for (const auto &action : snapshot.build_actions) {
    ImGui::PushID(action.identity.c_str());
    const auto opaque = action.identity.starts_with("blake3:") ||
                        compact_identifiers(action.identity) != action.identity;
    ImGui::Text("%s: %s", opaque ? "Build action" : action.identity.c_str(),
                build_action_state_name(action.state).data());
    ImGui::SameLine();
    if (ImGui::SmallButton("Details")) {
      ImGui::OpenPopup("action-details");
    }
    if (ImGui::BeginPopup("action-details")) {
      identifier_detail("Action", action.identity);
      for (const auto &dependency : action.dependencies) {
        identifier_detail("Requires", dependency);
      }
      ImGui::EndPopup();
    }
    if (!action.worker.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("worker %s", action.worker.c_str());
    }
    if (action.critical) {
      ImGui::SameLine();
      ImGui::TextDisabled("critical");
    }
    if (!action.scheduling_choice.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", action.scheduling_choice.c_str());
    }
    ImGui::PopID();
  }
  if (!snapshot.tests.empty()) {
    ImGui::Separator();
    ImGui::Text("Tests (%zu)", snapshot.tests.size());
    for (const auto &test : snapshot.tests) {
      ImGui::PushID(test.name.c_str());
      if (ImGui::SmallButton("Run")) {
        request_tests(test.name);
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(test.name.c_str());
      ImGui::PopID();
    }
  }
  for (const auto &result : snapshot.test_results) {
    ImGui::Text("%s: %s (%s)", result.name.c_str(),
                result.passed ? "passed" : "failed",
                result.hit ? "hit" : "ran");
    if (!result.output.empty()) {
      ImGui::TextWrapped("%s", result.output.c_str());
    }
  }
  if (snapshot.deploy.running || snapshot.deploy.failure ||
      !snapshot.deploy.references.empty()) {
    ImGui::Separator();
    if (snapshot.deploy.running) {
      ImGui::TextUnformatted("Pushing...");
    }
    if (snapshot.deploy.failure) {
      draw_user_error(describe_push_failure(*snapshot.deploy.failure,
                                            snapshot.deploy.remote,
                                            repository_path()),
                      "push-failure", [this] { request_push(); });
    }
    if (!snapshot.deploy.references.empty()) {
      ImGui::Text("Pushed to %s: %llu objects, %llu bytes",
                  lake_host(snapshot.deploy.remote).c_str(),
                  static_cast<unsigned long long>(snapshot.deploy.objects),
                  static_cast<unsigned long long>(snapshot.deploy.bytes));
    }
    for (const auto &reference : snapshot.deploy.references) {
      ImGui::Text("%s: %s", reference.name.c_str(),
                  push_outcome_name(reference.outcome).data());
      if (!reference.reason.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", reference.reason.c_str());
      }
    }
  }
  if (!snapshot.build_running && snapshot.build_result.empty() &&
      snapshot.build_actions.empty() && snapshot.tests.empty() &&
      snapshot.test_results.empty() && !snapshot.deploy.running &&
      !snapshot.deploy.failure && snapshot.deploy.references.empty() &&
      !snapshot.build_error) {
    ImGui::TextWrapped("%s",
                       panels_.descriptor(PanelId::build).empty_state.data());
  }
  ImGui::EndChild();
}

void App::draw_activity_bar() {
  const auto item = [&](PanelId id) {
    const auto &descriptor = panels_.descriptor(id);
    const auto placement = panels_.layout(id).placement;
    const auto selected = panels_.active(placement) == id &&
                          (placement != PanelPlacement::sidebar ||
                           shell_state_.sidebar_visible) &&
                          (placement != PanelPlacement::bottom ||
                           shell_state_.bottom_panel_visible);
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_Header));
    }
    if (ImGui::Button(
            ("##activity-" + std::string(panel_id_name(id))).c_str(),
            ImVec2(activity_bar_width - design::space2, design::control_md))) {
      if (selected && placement == PanelPlacement::sidebar) {
        shell_state_.sidebar_visible = !shell_state_.sidebar_visible;
      } else if (selected && placement == PanelPlacement::bottom) {
        shell_state_.bottom_panel_visible = !shell_state_.bottom_panel_visible;
      } else {
        panels_.focus(id);
        if (placement == PanelPlacement::sidebar) {
          shell_state_.sidebar_visible = true;
        } else if (placement == PanelPlacement::bottom) {
          shell_state_.bottom_panel_visible = true;
        }
      }
      persist_shell_state();
    }
    item_icon(panel_icon(id),
              (activity_bar_width - design::space2 - 16.0F) * 0.5F);
    if (selected) {
      ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", descriptor.title.data());
    }
  };
  item(PanelId::repository);
  item(PanelId::search);
  item(PanelId::review);
  item(PanelId::build);
  item(PanelId::multiplayer);
}

void App::draw_panel_menu(PanelId id) {
  const auto placement = panels_.layout(id).placement;
  const auto move_to = [this, id, placement](const char *label,
                                             PanelPlacement target) {
    if (ImGui::MenuItem(label, nullptr, false, placement != target)) {
      panels_.move(id, target);
      if (target == PanelPlacement::sidebar) {
        shell_state_.sidebar_visible = true;
      } else if (target == PanelPlacement::bottom) {
        shell_state_.bottom_panel_visible = true;
      }
      persist_shell_state();
    }
  };
  move_to("Dock in sidebar", PanelPlacement::sidebar);
  move_to("Dock in bottom area", PanelPlacement::bottom);
  move_to("Open as editor tab", PanelPlacement::editor_tab);
  if (placement == PanelPlacement::floating) {
    if (ImGui::MenuItem("Re-dock")) {
      panels_.redock(id);
      persist_shell_state();
    }
  } else {
    move_to("Float", PanelPlacement::floating);
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Reset panel layout")) {
    reset_panel_layout();
  }
}

void App::draw_panel_header(PanelId id) {
  if (panels_.layout(id).placement != PanelPlacement::sidebar) {
    return;
  }
  ImGui::PushID(panel_id_name(id).data());
  const auto label = icon_label(panel_label(id), 0.0F);
  type_heading(label, std::string("panel/") + std::string(panel_id_name(id)) +
                          "/title");
  item_icon(panel_icon(id));
  ImGui::SameLine();
  if (ImGui::SmallButton("...")) {
    ImGui::OpenPopup("panel-placement");
  }
  if (ImGui::BeginPopup("panel-placement")) {
    draw_panel_menu(id);
    ImGui::EndPopup();
  }
  ImGui::Separator();
  ImGui::PopID();
}

void App::draw_panel(PanelId id) {
  const auto position = ImGui::GetCursorScreenPos();
  const auto available = ImGui::GetContentRegionAvail();
  frame_layout_.panels[id].content = {position.x, position.y, available.x,
                                      available.y};
  draw_panel_header(id);
  switch (id) {
  case PanelId::repository:
    if (screenshot_mode()) {
      ImGui::TextDisabled("orama/liquify");
      ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      if (directory_item("src  [offline]")) {
        file_item("app.cpp  [modified]", NavigationFileKind::cpp_source, true);
        file_item("app_state.cpp  [in change]", NavigationFileKind::cpp_source);
        file_item("editor_buffer.cpp", NavigationFileKind::cpp_source);
        ImGui::TreePop();
      }
      ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      if (directory_item("docs  [not here]")) {
        file_item("ui  [conflicted]", NavigationFileKind::directory);
        ImGui::TreePop();
      }
      file_item("CMakeLists.txt  [online only]", NavigationFileKind::cmake);
      file_item("README.md", NavigationFileKind::markdown);
    } else if (repository_) {
      draw_repository_tree();
    } else {
      ImGui::TextWrapped("%s", panels_.descriptor(id).empty_state.data());
    }
    break;
  case PanelId::search:
    if (kit_button("Quick open", ButtonKind::secondary)) {
      invoke_navigation_action(NavigationAction::quick_open);
    }
    ImGui::SameLine();
    if (kit_button("Symbol search", ButtonKind::secondary)) {
      invoke_navigation_action(NavigationAction::symbol_search);
    }
    ImGui::Separator();
    text_field("Search", &sidebar_search_, "Text or /regular expression/");
    frame_layout_.controls["search/query"] = item_rectangle();
    text_field("Replace with", &replace_query_);
    frame_layout_.controls["search/replace"] = item_rectangle();
    checkbox("Regular expression", &find_options_.regular_expression);
    if (kit_button("Find", ButtonKind::primary)) {
      if (editor_graph_) {
        const auto query = find_options_.regular_expression
                               ? "/" + sidebar_search_ + "/"
                               : "\"" + sidebar_search_ + "\"";
        editor_graph_->request_search(query);
      }
    }
    ImGui::SameLine();
    if (kit_button("Replace all", ButtonKind::secondary) && editor_graph_) {
      editor_graph_->request_replace(sidebar_search_, replace_query_,
                                     find_options_.regular_expression);
    }
    ImGui::Separator();
    if (screenshot_mode()) {
      ImGui::TextUnformatted("3 repository results");
      ImGui::Selectable("src/app.cpp:6  repository");
      ImGui::Selectable("src/app.cpp:7  repository");
      ImGui::Selectable("docs/ui/editor.md:18  repository configuration");
    } else if (!repository_) {
      ImGui::TextWrapped("%s", panels_.descriptor(id).empty_state.data());
    } else {
      const auto snapshot = editor_graph_->snapshot();
      if (!snapshot.search.error.empty()) {
        ImGui::TextWrapped("%s", snapshot.search.error.c_str());
      } else {
        ImGui::Text(
            "%zu results · %llu replacements", snapshot.search.hits.size(),
            static_cast<unsigned long long>(snapshot.search.replacements));
        for (std::size_t index = 0; index < snapshot.search.hits.size();
             ++index) {
          const auto &hit = snapshot.search.hits[index];
          const auto label = hit.path + ":" + std::to_string(hit.line) + ":" +
                             std::to_string(hit.column) + "  " + hit.snippet +
                             "##search-hit-" + std::to_string(index);
          if (ImGui::Selectable(label.c_str())) {
            open_editor(hit.path, "search:" + hit.path);
            editor_->set_cursor(
                {hit.line == 0 ? 0 : static_cast<std::size_t>(hit.line - 1),
                 hit.column == 0 ? 0
                                 : static_cast<std::size_t>(hit.column - 1)});
            ensure_cursor_visible();
          }
        }
      }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Symbols");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##repository-symbol-filter", "Filter symbols",
                             &symbol_search_query_);
    if (!repository_) {
      ImGui::TextDisabled("Open a repository to search symbols.");
    } else if (repository_->navigation_loading()) {
      ImGui::TextDisabled("Indexing libliquid symbol records...");
    } else {
      for (const auto &match : rank_symbols(repository_->navigation_symbols(),
                                            symbol_search_query_, 12)) {
        const auto &symbol = repository_->navigation_symbols()[match.index];
        const auto label = symbol.name + "  " + symbol.path.generic_string() +
                           "##side-" + std::to_string(match.index);
        if (ImGui::Selectable(label.c_str())) {
          open_navigation_location({symbol.path, symbol.range.start_line,
                                    symbol.range.start_column});
        }
      }
    }
    break;
  case PanelId::review:
    if (screenshot_mode()) {
      ImGui::TextDisabled("Change #31");
      ImGui::Selectable("App shell on the Orama design system", true);
      ImGui::Spacing();
      ImGui::TextDisabled("8 files changed");
      ImGui::TextUnformatted("2 comments");
      ImGui::Selectable("src/app.cpp:584  Active");
      ImGui::Selectable("docs/ui/app-shell.md:12  Moved");
      ImGui::Spacing();
      ImGui::Button("Approve");
      ImGui::SameLine();
      ImGui::Button("Request changes");
    } else {
      draw_review_panel();
    }
    break;
  case PanelId::build:
    draw_build_panel(editor_graph_ ? editor_graph_->snapshot()
                                   : EditorGraphSnapshot{});
    break;
  case PanelId::multiplayer:
    draw_multiplayer_details();
    break;
  case PanelId::diagnostics:
    draw_diagnostics_panel(editor_graph_ ? editor_graph_->snapshot()
                                         : EditorGraphSnapshot{});
    break;
  case PanelId::assistant:
    if (screenshot_mode()) {
      ImGui::TextDisabled("Off · no provider exists and no connection is open");
      ImGui::InputTextWithHint("##demo-assistant",
                               "Ask about the current definition",
                               &assistant_prompt_);
      ImGui::Button("Ask");
      ImGui::SameLine();
      ImGui::Button("Explain diagnostic");
      ImGui::SameLine();
      ImGui::Button("Propose edit");
    } else if (assistant_) {
      draw_assistant();
    } else {
      ImGui::TextWrapped("%s", panels_.descriptor(id).empty_state.data());
    }
    break;
  case PanelId::plugins:
    if (screenshot_mode()) {
      ImGui::TextUnformatted("orama.sample/status: Sample plugin");
      ImGui::TextDisabled("Index and formatter commands ready.");
    } else if (plugin_failures_.empty() && plugin_untrusted_.empty() &&
               plugin_panels_.empty()) {
      ImGui::TextWrapped("%s", panels_.descriptor(id).empty_state.data());
    } else {
      draw_plugin_panels();
    }
    break;
  case PanelId::structural_editing:
    draw_structural_panel();
    break;
  }
}

void App::draw_sidebar() {
  const auto active = panels_.active(PanelPlacement::sidebar);
  if (!active) {
    ImGui::TextWrapped("No panel is docked in the sidebar.");
    return;
  }
  draw_panel(*active);
  if (panels_.focus_requested(*active)) {
    panels_.clear_focus_request();
  }
}

void App::draw_split_editor() {
  ImGui::TextDisabled("Secondary split — read-only overview");
  ImGui::Separator();
  if (!editor_) {
    return;
  }
  auto *editor_font = ImGui::GetIO().Fonts->Fonts[editor_font_index_];
  ImGui::PushFont(editor_font);
  const auto line_count = std::min<std::size_t>(editor_->line_count(), 40);
  for (std::size_t line = 0; line < line_count; ++line) {
    ImGui::TextUnformatted(editor_->line(line).data(),
                           editor_->line(line).data() +
                               editor_->line(line).size());
  }
  ImGui::PopFont();
}

void App::draw_editor_area() {
  const auto editor_panels = panels_.panels_at(PanelPlacement::editor_tab);
  const auto has_visible_panel = std::ranges::any_of(
      editor_panels, [this](PanelId id) { return panels_.layout(id).visible; });
  if (!editor_ && !has_visible_panel) {
    // Centered as one block: title, supporting copy, the next step.
    const auto available = ImGui::GetContentRegionAvail();
    const auto origin = ImGui::GetCursorPos();
    const auto hint = std::string_view{"Select a file in the repository sidebar to "
                                       "start editing."};
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[title_font_index_]);
    const auto title_width = ImGui::CalcTextSize("liquify").x;
    const auto title_height = ImGui::GetTextLineHeightWithSpacing();
    ImGui::PopFont();
    const auto hint_width =
        ImGui::CalcTextSize(hint.data(), hint.data() + hint.size()).x;
    const auto top = origin.y +
                     std::max((available.y - title_height -
                               ImGui::GetTextLineHeightWithSpacing() * 2.0F) *
                                  0.42F,
                              design::space8);
    ImGui::SetCursorPos(
        ImVec2(origin.x + std::max((available.x - title_width) * 0.5F, 0.0F), top));
    type_title("liquify", "editor/empty-title");
    const auto copy = std::string_view{"The IDE for liquid"};
    ImGui::SetCursorPosX(origin.x + std::max((available.x -
                                              ImGui::CalcTextSize(copy.data(), copy.data() + copy.size()).x) *
                                                 0.5F,
                                             0.0F));
    ImGui::TextDisabled("%s", copy.data());
    ImGui::SetCursorPosX(origin.x +
                         std::max((available.x - hint_width) * 0.5F, 0.0F));
    ImGui::TextUnformatted(hint.data(), hint.data() + hint.size());
    return;
  }

  if (ImGui::BeginTabBar("editor-tabs", ImGuiTabBarFlags_Reorderable)) {
    if (editor_) {
      const auto path = std::string(editor_->display_path());
      const auto label =
          path + (editor_->dirty() ? " *" : "") + "###active-editor";
      if (ImGui::BeginTabItem(label.c_str())) {
        if (shell_state_.editor_split) {
          const auto available = ImGui::GetContentRegionAvail();
          const auto content_width = std::max(available.x - divider_size, 1.0F);
          const auto primary_width =
              content_width * shell_state_.editor_split_ratio;
          ImGui::BeginChild("primary-editor",
                            ImVec2(primary_width, available.y));
          draw_editor();
          ImGui::EndChild();
          ImGui::SameLine(0.0F, 0.0F);
          ImGui::InvisibleButton("editor-split-divider",
                                 ImVec2(divider_size, available.y));
          if (ImGui::IsItemActive()) {
            const auto change = ImGui::GetIO().MouseDelta.x / content_width;
            shell_state_.editor_split_ratio = std::clamp(
                shell_state_.editor_split_ratio + change, 0.25F, 0.75F);
          }
          if (ImGui::IsItemDeactivatedAfterEdit()) {
            persist_shell_state();
          }
          ImGui::SameLine(0.0F, 0.0F);
          ImGui::BeginChild(
              "secondary-editor",
              ImVec2(std::max(content_width - primary_width, 1.0F),
                     available.y),
              ImGuiChildFlags_Border);
          draw_split_editor();
          ImGui::EndChild();
        } else {
          draw_editor();
        }
        ImGui::EndTabItem();
      }
    }
    for (const auto id : editor_panels) {
      if (!panels_.layout(id).visible) {
        continue;
      }
      auto open = true;
      const auto label =
          icon_label(panel_label(id), ImGui::GetStyle().FramePadding.x) +
          "###panel-" + std::string(panel_id_name(id));
      const auto flags =
          panels_.focus_requested(id) ? ImGuiTabItemFlags_SetSelected : 0;
      const auto selected = ImGui::BeginTabItem(label.c_str(), &open, flags);
      frame_layout_.panels[id].tab = item_rectangle();
      frame_layout_.panels[id].selected = selected;
      item_icon(panel_icon(id));
      if (ImGui::BeginPopupContextItem()) {
        draw_panel_menu(id);
        ImGui::EndPopup();
      }
      if (panels_.focus_requested(id)) {
        panels_.clear_focus_request();
      }
      if (ImGui::IsItemActivated()) {
        panels_.focus(id);
        panels_.clear_focus_request();
        persist_shell_state();
      }
      if (selected) {
        draw_panel(id);
        ImGui::EndTabItem();
      }
      if (!open) {
        panels_.set_visible(id, false);
        persist_shell_state();
      }
    }
    ImGui::EndTabBar();
  }
}

void App::draw_bottom_panel() {
  const auto active = panels_.active(PanelPlacement::bottom);
  auto first = true;
  for (const auto id : panels_.panels_at(PanelPlacement::bottom)) {
    if (!panels_.layout(id).visible) {
      continue;
    }
    const auto label = icon_label(panel_label(id), 0.0F);
    const auto width = ImGui::CalcTextSize(label.c_str()).x + 16.0F;
    if (!first && ImGui::GetContentRegionAvail().x > width) {
      ImGui::SameLine();
    }
    first = false;
    if (ImGui::Selectable(label.c_str(), active == id, 0,
                          ImVec2(width, 24.0F))) {
      panels_.focus(id);
      panels_.clear_focus_request();
      persist_shell_state();
    }
    frame_layout_.panels[id].tab = item_rectangle();
    frame_layout_.panels[id].selected = active == id;
    item_icon(panel_icon(id));
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s · Right-click for panel placement",
                        panels_.descriptor(id).title.data());
    }
    if (ImGui::BeginPopupContextItem()) {
      draw_panel_menu(id);
      ImGui::EndPopup();
    }
  }
  ImGui::Separator();
  if (active) {
    ImGui::BeginChild("panel-content");
    draw_panel(*active);
    ImGui::EndChild();
    if (panels_.focus_requested(*active)) {
      panels_.clear_focus_request();
    }
  }
}

void App::draw_floating_panels() {
  for (const auto id : panels_.panels_at(PanelPlacement::floating)) {
    const auto &current = panels_.layout(id);
    if (!current.visible) {
      continue;
    }
    const auto rect = current.floating_rect;
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height),
                             ImGuiCond_FirstUseEver);
    if (panels_.focus_requested(id)) {
      ImGui::SetNextWindowFocus();
      panels_.clear_focus_request();
    }
    auto open = true;
    const auto title =
        panel_label(id) + "###floating-" + std::string(panel_id_name(id));
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
      if (ImGui::SmallButton("...##floating-placement")) {
        ImGui::OpenPopup("floating-placement");
      }
      if (ImGui::BeginPopup("floating-placement")) {
        draw_panel_menu(id);
        ImGui::EndPopup();
      }
      draw_panel(id);
    }
    const auto position = ImGui::GetWindowPos();
    const auto size = ImGui::GetWindowSize();
    ImGui::End();
    panels_.update_floating_rect(id, {position.x, position.y, size.x, size.y});
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      persist_shell_state();
    }
    if (!open) {
      panels_.set_visible(id, false);
      persist_shell_state();
    }
  }
}

auto App::repository_path() const -> std::filesystem::path {
  return repository_ ? repository_->path() : std::filesystem::path{};
}

// The failure the status bar names, and the panel that shows it. Build and push failures live
// in the Build panel; code-analysis failures in Diagnostics.
auto App::status_failure(const EditorGraphSnapshot &graph) const
    -> std::optional<StatusFailure> {
  if (graph.build_error) {
    return StatusFailure{
        describe_build_failure(*graph.build_error, repository_path()).summary,
        PanelId::build};
  }
  if (graph.deploy.failure) {
    return StatusFailure{describe_push_failure(*graph.deploy.failure,
                                               graph.deploy.remote,
                                               repository_path())
                             .summary,
                         PanelId::build};
  }
  if (!graph.query_error.empty()) {
    return StatusFailure{
        describe_query_failure(graph_query_failure(graph), repository_path())
            .summary,
        PanelId::diagnostics};
  }
  return std::nullopt;
}

// Actions a failure can offer besides Retry, which the caller supplies. An action liquify
// cannot perform yet is not drawn: a button that does nothing is worse than none.
auto App::error_action_available(ErrorAction action) const -> bool {
  return (action == ErrorAction::set_sync_remote ||
          action == ErrorAction::set_up_build) &&
         repository_ != nullptr;
}

void App::run_error_action(ErrorAction action) {
  if (action == ErrorAction::set_sync_remote) {
    open_set_lake_dialog();
  } else if (action == ErrorAction::set_up_build) {
    open_build_setup();
  }
}

void App::open_build_setup() {
  if (!repository_) {
    editor_status_ = "Open a repository to set up its build";
    return;
  }
  auto names = std::vector<std::string>{};
  for (const auto &entry : repository_->entries()) {
    names.push_back(entry.name);
  }
  build_proposal_ = propose_build_command(names);
  build_setup_command_ = build_proposal_.command;
  build_setup_failure_ = {};
  build_setup_requested_ = true;
}

void App::poll_build_setup() {
  if (!build_setup_operation_ ||
      build_setup_operation_->wait_for(std::chrono::milliseconds::zero()) !=
          std::future_status::ready) {
    return;
  }
  build_setup_failure_ = build_setup_operation_->get();
  build_setup_operation_.reset();
  if (build_setup_failure_.empty()) {
    build_setup_close_requested_ = true;
    editor_status_ = "Build recorded: " + build_setup_command_;
    request_build();
  }
}

void App::draw_build_setup_dialog() {
  poll_build_setup();
  if (build_setup_requested_) {
    ImGui::OpenPopup("Set Up Build");
    build_setup_requested_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(600.0F, 0.0F), ImGuiCond_Appearing);
  if (!begin_dialog("Set Up Build", ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  if (build_setup_close_requested_) {
    build_setup_close_requested_ = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  ImGui::PushTextWrapPos(580.0F);
  ImGui::TextUnformatted(
      "liquid records the command that builds this repository once, by "
      "running it, then F7 builds with it.");
  ImGui::TextDisabled("%s", build_proposal_.reason.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::TextUnformatted("Build command");
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(580.0F);
  ImGui::InputText("##build-command", &build_setup_command_);
  frame_layout_.controls["build-setup/command"] = item_rectangle();
  ImGui::TextDisabled("Runs in the repository folder with /bin/sh.");
  if (!build_setup_failure_.empty()) {
    draw_user_error(build_setup_failure_, "build-setup-failure");
  }
  ImGui::Spacing();
  ImGui::BeginDisabled(build_setup_command_.empty() ||
                       build_setup_operation_.has_value());
  if (kit_button(build_setup_operation_ ? "Recording..." : "Record and build",
                 ButtonKind::primary)) {
    build_setup_failure_ = {};
    build_setup_operation_.emplace(std::async(
        std::launch::async,
        [path = repository_path(), command = build_setup_command_] {
          return record_build_command(path, command);
        }));
    request_repaint();
  }
  frame_layout_.controls["build-setup/submit"] = item_rectangle();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::open_set_lake_dialog() {
  if (!repository_) {
    editor_status_ = "Open a repository to choose its lake";
    return;
  }
  set_lake_requested_ = true;
  set_lake_failure_ = {};
  set_lake_url_ = options_.lake_url.value_or(std::string{});
}

void App::draw_set_lake_dialog() {
  if (set_lake_requested_) {
    ImGui::OpenPopup("Set Lake");
    set_lake_requested_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(600.0F, 0.0F), ImGuiCond_Appearing);
  if (!begin_dialog("Set Lake", ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::PushTextWrapPos(580.0F);
  ImGui::TextUnformatted(
      "Choose the lake repository this one pushes to and is reviewed on. Copy "
      "its address from the repository's page on lake.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::TextUnformatted("Lake repository address");
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(580.0F);
  ImGui::InputTextWithHint("##set-lake-url",
                           "https://lake.example/v1/repositories/…",
                           &set_lake_url_);
  frame_layout_.controls["set-lake/url"] = item_rectangle();
  if (!set_lake_failure_.empty()) {
    draw_user_error(set_lake_failure_, "set-lake-failure");
  }
  ImGui::Spacing();
  ImGui::BeginDisabled(set_lake_url_.empty());
  if (kit_button("Set lake", ButtonKind::primary)) {
    set_lake_failure_ = record_lake_remote(repository_path(), set_lake_url_);
    if (set_lake_failure_.empty()) {
      options_.lake_url = set_lake_url_;
      lake_url_from_repository_ = true;
      repository_->set_lake_remote(set_lake_url_);
      review_.reset();
      configure_review();
      editor_status_ = "Pushes to " + lake_host(set_lake_url_);
      ImGui::CloseCurrentPopup();
    }
  }
  frame_layout_.controls["set-lake/submit"] = item_rectangle();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// The controls kit (liquify#101), mirroring lake's: button kinds, a checkbox that shows its
// state, labels above full-width fields, and sliders with one handle shape.
auto App::kit_button(const char *label, ButtonKind kind, float width,
                     float height) -> bool {
  const auto size = ImVec2(width, height);
  const auto palette =
      effective_palette(applied_dark_theme_, applied_high_contrast_);
  auto pushed = 0;
  const auto push = [&pushed](ImGuiCol slot, design::Rgba color) {
    ImGui::PushStyleColor(slot, imgui_color(color));
    ++pushed;
  };
  if (kind == ButtonKind::primary) {
    push(ImGuiCol_Button, palette.action);
    push(ImGuiCol_ButtonHovered, palette.action_hover);
    push(ImGuiCol_ButtonActive, palette.action_pressed);
    push(ImGuiCol_Text, design::Rgba{1.0F, 1.0F, 1.0F, 1.0F});
    push(ImGuiCol_Border, palette.action);
  } else if (kind == ButtonKind::destructive) {
    push(ImGuiCol_Button, palette.surface);
    push(ImGuiCol_ButtonHovered, palette.danger_soft);
    push(ImGuiCol_Text, palette.danger);
    push(ImGuiCol_Border, palette.danger);
  } else if (kind == ButtonKind::quiet) {
    push(ImGuiCol_Button, design::Rgba{0.0F, 0.0F, 0.0F, 0.0F});
    push(ImGuiCol_Border, design::Rgba{0.0F, 0.0F, 0.0F, 0.0F});
  } else {
    push(ImGuiCol_Border, palette.border_strong);
  }
  const auto pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(pushed);
  return pressed;
}

auto App::checkbox(const char *label, bool *value) -> bool {
  const auto palette =
      effective_palette(applied_dark_theme_, applied_high_contrast_);
  const auto checked = *value;
  ImGui::PushStyleColor(ImGuiCol_Border, imgui_color(checked ? palette.action
                                                             : palette.border_strong));
  // A compact box, as lake's: the frame padding sets the square's size.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(ImGui::GetStyle().FramePadding.x, design::space1 * 0.5F));
  ImGui::PushStyleColor(ImGuiCol_FrameBg,
                        imgui_color(checked ? palette.action : palette.surface));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                        imgui_color(checked ? palette.action_hover : palette.layer_hover));
  ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
  const auto changed = ImGui::Checkbox(label, value);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  return changed;
}

void App::field_label(const char *label) {
  ImGui::TextUnformatted(label);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

auto App::text_field(const char *label, std::string *value, const char *hint) -> bool {
  field_label(label);
  const auto id = std::string("##") + label;
  return hint == nullptr ? ImGui::InputText(id.c_str(), value)
                         : ImGui::InputTextWithHint(id.c_str(), hint, value);
}

// One handle shape for every slider: an integer slider's grab otherwise widens with a small
// range, which is why the two size sliders in Settings looked different.
auto App::slider_int(const char *label, int *value, int minimum, int maximum) -> bool {
  field_label(label);
  auto fraction = static_cast<float>(*value);
  const auto id = std::string("##") + label;
  if (!ImGui::SliderFloat(id.c_str(), &fraction, static_cast<float>(minimum),
                          static_cast<float>(maximum), "%.0f")) {
    return false;
  }
  const auto rounded = static_cast<int>(std::lround(fraction));
  if (rounded == *value) {
    return false;
  }
  *value = rounded;
  return true;
}

// The type scale (liquify#100), from the design tokens lake uses: a title (24, semibold), a
// section heading (16, semibold), body (the UI size) and captions (disabled ink).
void App::type_title(std::string_view text, std::string_view id) {
  ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[title_font_index_]);
  ImGui::TextUnformatted(text.data(), text.data() + text.size());
  ImGui::PopFont();
  if (!id.empty()) {
    frame_layout_.controls[std::string(id)] = item_rectangle();
  }
}

void App::type_heading(std::string_view text, std::string_view id) {
  ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[section_font_index_]);
  ImGui::TextUnformatted(text.data(), text.data() + text.size());
  ImGui::PopFont();
  if (!id.empty()) {
    frame_layout_.controls[std::string(id)] = item_rectangle();
  }
}

// A modal dialog whose title bar uses the heading face. Every dialog title is Title Case.
auto App::begin_dialog(const char *title, int flags) -> bool {
  ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[section_font_index_]);
  const auto open = ImGui::BeginPopupModal(title, nullptr, flags);
  ImGui::PopFont();
  return open;
}

void App::draw_user_error(const UserError &failure, std::string_view id,
                          const std::function<void()> &retry) {
  const auto palette =
      effective_palette(applied_dark_theme_, applied_high_contrast_);
  ImGui::PushID(id.data(), id.data() + id.size());
  ImGui::PushTextWrapPos(0.0F);
  ImGui::PushStyleColor(ImGuiCol_Text, imgui_color(palette.danger));
  ImGui::TextUnformatted(failure.summary.c_str());
  ImGui::PopStyleColor();
  frame_layout_.controls[std::string(id) + "/summary"] = item_rectangle();
  if (!failure.next_step.empty()) {
    ImGui::TextUnformatted(failure.next_step.c_str());
  }
  ImGui::PopTextWrapPos();
  const auto has_action =
      !failure.action_label.empty() &&
      (failure.action == ErrorAction::retry
           ? static_cast<bool>(retry)
           : error_action_available(failure.action));
  if (has_action) {
    if (ImGui::Button(failure.action_label.c_str())) {
      if (failure.action == ErrorAction::retry) {
        retry();
      } else {
        run_error_action(failure.action);
      }
    }
    frame_layout_.controls[std::string(id) + "/action"] = item_rectangle();
  }
  if (!failure.details.empty()) {
    if (has_action) {
      ImGui::SameLine();
    }
    auto &open = error_details_open_[std::string(id)];
    if (ImGui::SmallButton(open ? "Hide details" : "Details")) {
      open = !open;
    }
    frame_layout_.controls[std::string(id) + "/details"] = item_rectangle();
    if (open) {
      ImGui::PushTextWrapPos(0.0F);
      ImGui::PushStyleColor(ImGuiCol_Text, imgui_color(palette.text_secondary));
      ImGui::TextUnformatted(failure.details.c_str());
      ImGui::PopStyleColor();
      ImGui::PopTextWrapPos();
    }
  }
  ImGui::PopID();
}

void App::open_cut_composer() {
  if (!repository_) {
    editor_status_ = "Open a repository to cut a change";
    return;
  }
  cut_composer_requested_ = true;
  cut_restore_editor_focus_ = editor_focused_;
  editor_focused_ = false;
  cut_message_.clear();
  cut_result_.reset();
  cut_failure_ = {};
  cut_pending_.reset();
  cut_preview_.emplace(std::async(
      std::launch::async,
      [path = repository_->path()] { return load_pending_change(path); }));
  request_repaint();
}

void App::poll_cut_composer() {
  if (cut_preview_ &&
      cut_preview_->wait_for(std::chrono::milliseconds::zero()) ==
          std::future_status::ready) {
    cut_pending_ = cut_preview_->get();
    cut_preview_.reset();
  }
  if (cut_operation_ &&
      cut_operation_->wait_for(std::chrono::milliseconds::zero()) ==
          std::future_status::ready) {
    auto result = cut_operation_->get();
    cut_operation_.reset();
    if (!result.failure.empty()) {
      cut_failure_ = std::move(result.failure);
      return;
    }
    cut_result_ = result.change;
    const auto summary = cut_message_summary(cut_message_);
    editor_status_ = "Cut change: " + summary;
    cut_close_requested_ = true;
    if (editor_graph_) {
      editor_graph_->refresh_status();
    }
  }
}

void App::draw_cut_composer() {
  poll_cut_composer();
  if (cut_composer_requested_) {
    ImGui::OpenPopup("Cut Change");
    cut_composer_requested_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(620.0F, 0.0F), ImGuiCond_Appearing);
  if (!begin_dialog("Cut Change", ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  if (cut_close_requested_) {
    cut_close_requested_ = false;
    editor_focused_ = cut_restore_editor_focus_;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  ImGui::PushTextWrapPos(600.0F);
  ImGui::TextUnformatted(
      "A change names the drips you saved since the last change. Push sends "
      "changes to lake, where they can be reviewed.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  if (!cut_pending_) {
    ImGui::TextDisabled("Reading saved drips...");
  } else if (!cut_pending_->failure.empty()) {
    draw_user_error(cut_pending_->failure, "cut-pending-failure");
  } else {
    if (cut_pending_->base.empty()) {
      ImGui::TextDisabled("This will be the repository's first change.");
    } else {
      ImGui::TextDisabled("Builds on change %s", cut_pending_->base.c_str());
    }
    if (cut_pending_->drips.empty()) {
      ImGui::TextUnformatted(
          "Nothing to cut yet. Each save is a drip; save an edit first.");
    } else {
      ImGui::Text("Gathers %zu %s touching %zu %s:", cut_pending_->drips.size(),
                  cut_pending_->drips.size() == 1 ? "drip" : "drips",
                  cut_pending_->paths.size(),
                  cut_pending_->paths.size() == 1 ? "file" : "files");
      for (const auto &path : cut_pending_->paths) {
        ImGui::BulletText("%s", path.c_str());
      }
      if (ImGui::TreeNode("Drips")) {
        for (const auto &drip : cut_pending_->drips) {
          auto files = std::string{};
          for (const auto &path : drip.paths) {
            files += (files.empty() ? "" : ", ") + path;
          }
          ImGui::TextDisabled("%s · %s · %s", drip.id.c_str(),
                              std::format("{:%Y-%m-%d %H:%M}", drip.at).c_str(),
                              files.c_str());
        }
        ImGui::TreePop();
      }
    }
  }
  if (editor_ && editor_->dirty()) {
    ImGui::TextColored(
        imgui_color(effective_palette(applied_dark_theme_,
                                      applied_high_contrast_)
                        .warning),
        "%s has unsaved edits, which are not in this change. Save first to "
        "include them.",
        std::string(editor_->display_path()).c_str());
  }
  ImGui::Spacing();
  ImGui::TextUnformatted("Message");
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::InputTextMultiline("##cut-message", &cut_message_,
                            ImVec2(600.0F, ImGui::GetTextLineHeight() * 4.0F));
  frame_layout_.controls["cut/message"] = item_rectangle();
  ImGui::TextDisabled("The first line is the change's title.");
  if (!cut_failure_.empty()) {
    draw_user_error(cut_failure_, "cut-failure");
  }
  ImGui::Spacing();
  const auto ready = cut_pending_ && cut_pending_->failure.empty() &&
                     !cut_pending_->drips.empty() &&
                     !cut_message_summary(cut_message_).empty() &&
                     !cut_operation_;
  ImGui::BeginDisabled(!ready);
  if (kit_button(cut_operation_ ? "Cutting..." : "Cut change", ButtonKind::primary)) {
    cut_failure_ = {};
    cut_operation_.emplace(std::async(
        std::launch::async,
        [path = repository_path(), message = cut_message_,
         session = multiplayer_session_]() mutable {
          return cut_change(path, std::move(message), session);
        }));
    request_repaint();
  }
  frame_layout_.controls["cut/submit"] = item_rectangle();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    editor_focused_ = cut_restore_editor_focus_;
    ImGui::CloseCurrentPopup();
  }
  frame_layout_.controls["cut/cancel"] = item_rectangle();
  ImGui::EndPopup();
}

void App::draw_clone_dialog() {
  if (clone_dialog_open_) {
    ImGui::OpenPopup("Clone from Lake");
    clone_dialog_open_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(600.0F, 0.0F), ImGuiCond_Appearing);
  if (!begin_dialog("Clone from Lake", ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::PushTextWrapPos(580.0F);
  ImGui::TextUnformatted(
      "Copy a repository from lake into a new folder on this computer. Paste "
      "its address from the repository's page on lake.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::TextUnformatted("Lake repository address");
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(580.0F);
  ImGui::InputTextWithHint("##clone-remote",
                           "https://lake.example/v1/repositories/…",
                           &clone_remote_);
  frame_layout_.controls["clone/remote"] = item_rectangle();
  ImGui::TextUnformatted("New folder");
  ImGui::SetNextItemWidth(580.0F);
  ImGui::InputTextWithHint("##clone-folder", "/Users/you/projects/name",
                           &clone_path_);
  frame_layout_.controls["clone/folder"] = item_rectangle();
  ImGui::TextDisabled("The folder must not exist yet, or be empty.");
  ImGui::Spacing();
  const auto ready = !clone_remote_.empty() && !clone_path_.empty();
  ImGui::BeginDisabled(!ready);
  if (kit_button("Clone", ButtonKind::primary)) {
    begin_clone_repository(clone_remote_, clone_path_);
    ImGui::CloseCurrentPopup();
  }
  frame_layout_.controls["clone/submit"] = item_rectangle();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  frame_layout_.controls["clone/cancel"] = item_rectangle();
  ImGui::EndPopup();
}

void App::begin_create_repository(std::filesystem::path path) {
  if (repository_operation_) {
    return;
  }
  first_run_.begin_open(path);
  repository_operation_is_clone_ = false;
  request_repaint();
  repository_operation_.emplace(
      std::async(std::launch::async, [path = std::move(path)] {
        {
          auto created =
              liquid::runtime::block_on(liquid::repository::create(path));
          if (!created) {
            throw std::runtime_error(
                created.error().code == liquid::errc::already_exists
                    ? std::string{"this folder is already a liquid "
                                  "repository; open it instead"}
                    : without_absolute_paths(created.error().message, path));
          }
        }
        return std::make_unique<RepositoryView>(RepositoryView::open(path));
      }));
}

void App::draw_new_repository_dialog() {
  if (new_repository_dialog_open_) {
    ImGui::OpenPopup("New Repository");
    new_repository_dialog_open_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(600.0F, 0.0F), ImGuiCond_Appearing);
  if (!begin_dialog("New Repository", ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::PushTextWrapPos(580.0F);
  ImGui::TextUnformatted(
      "Start a liquid repository in a new or empty folder. Every save becomes "
      "a drip; cut drips into changes and push them to lake when you are "
      "ready.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::TextUnformatted("Folder");
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(460.0F);
  ImGui::InputTextWithHint("##new-repository-folder",
                           "/Users/you/projects/name", &new_repository_path_);
  frame_layout_.controls["new-repository/folder"] = item_rectangle();
  ImGui::SameLine();
  if (ImGui::Button("Choose…")) {
    SDL_ShowOpenFolderDialog(
        [](void *userdata, const char *const *files, int) {
          auto *app = static_cast<App *>(userdata);
          if (files != nullptr && files[0] != nullptr) {
            app->new_repository_path_ = files[0];
          }
        },
        this, window_, nullptr, false);
  }
  ImGui::Spacing();
  ImGui::BeginDisabled(new_repository_path_.empty());
  if (kit_button("Create repository", ButtonKind::primary)) {
    begin_create_repository(new_repository_path_);
    ImGui::CloseCurrentPopup();
  }
  frame_layout_.controls["new-repository/submit"] = item_rectangle();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::draw_welcome() {
  const auto available = ImGui::GetContentRegionAvail();
  const auto card_width = std::min(available.x - design::space8 * 2.0F, 920.0F);
  ImGui::SetCursorPosX(
      std::max((available.x - card_width) * 0.5F, design::space4));
  ImGui::SetCursorPosY(design::space8);
  ImGui::BeginChild(
      "first-run-card",
      ImVec2(card_width, std::max(available.y - design::space8 * 2.0F, 1.0F)),
      ImGuiChildFlags_Border, ImGuiWindowFlags_AlwaysUseWindowPadding);
  type_title("Welcome to liquify", "welcome/title");
  ImGui::PushStyleColor(ImGuiCol_Text,
                        imgui_color(effective_palette(applied_dark_theme_,
                                                      applied_high_contrast_)
                                        .text_secondary));
  ImGui::TextUnformatted(
      "Build software locally. Collaborate through lake when it is reachable.");
  frame_layout_.controls["welcome/copy"] = item_rectangle();
  ImGui::PopStyleColor();
  ImGui::TextDisabled("Open or create a repository here, or clone one from "
                      "lake.");
  ImGui::Spacing();

  const auto phase = first_run_.phase();
  if (phase == RepositoryLifecyclePhase::unreadable) {
    draw_user_error(
        {.kind = FailureKind::local,
         .summary = "This folder could not be opened as a repository",
         .next_step = "Retry, or choose another folder.",
         .details = without_absolute_paths(first_run_.error(),
                                           first_run_.pending_path())},
        "open-failure");
    ImGui::TextDisabled("%s", first_run_.pending_path().string().c_str());
    if (kit_button("Retry", ButtonKind::primary)) {
      begin_open_repository(first_run_.pending_path());
    }
    ImGui::SameLine();
    if (ImGui::Button("Choose another folder")) {
      show_folder_dialog();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back")) {
      first_run_.return_to_welcome();
    }
    ImGui::Separator();
  } else if (phase == RepositoryLifecyclePhase::offline ||
             phase == RepositoryLifecyclePhase::failed) {
    draw_user_error(first_run_.clone_failure(), "clone-failure");
    if (kit_button("Retry clone", ButtonKind::primary)) {
      begin_clone_repository(first_run_.clone_remote(),
                             first_run_.pending_path());
    }
    frame_layout_.controls["clone-failure/retry"] = item_rectangle();
    ImGui::SameLine();
    if (ImGui::Button("Back")) {
      first_run_.return_to_welcome();
    }
    frame_layout_.controls["clone-failure/back"] = item_rectangle();
    ImGui::Separator();
  } else if (phase == RepositoryLifecyclePhase::opening ||
             phase == RepositoryLifecyclePhase::cloning) {
    ImGui::TextUnformatted(phase == RepositoryLifecyclePhase::opening
                               ? "Opening repository..."
                               : "Cloning repository...");
    ImGui::TextDisabled("%s", first_run_.pending_path().string().c_str());
    ImGui::Separator();
  }

  ImGui::BeginDisabled(repository_operation_.has_value());
  if (kit_button("Open folder", ButtonKind::primary, 150.0F, 38.0F)) {
    show_folder_dialog();
  }
  frame_layout_.controls["front-door/open"] = item_rectangle();
  ImGui::SameLine();
  if (ImGui::Button("New repository", ImVec2(150.0F, 38.0F))) {
    new_repository_dialog_open_ = true;
  }
  frame_layout_.controls["front-door/new"] = item_rectangle();
  ImGui::SameLine();
  if (ImGui::Button("Clone from lake", ImVec2(150.0F, 38.0F))) {
    clone_dialog_open_ = true;
    clone_remote_.clear();
    clone_path_.clear();
  }
  frame_layout_.controls["front-door/clone"] = item_rectangle();
  ImGui::SameLine();
  if (ImGui::Button("New window", ImVec2(150.0F, 38.0F))) {
    open_new_window();
  }
  frame_layout_.controls["front-door/new-window"] = item_rectangle();
  ImGui::EndDisabled();
  ImGui::Spacing();

  // This is the computer's own signing identity (TAAP), not a lake account: it signs every
  // drip and change. Signing in to a lake comes with choosing one (liquify#96).
  if (first_run_.session()) {
    if (ImGui::SmallButton("Your identity")) {
      ImGui::OpenPopup("identity-details");
    }
    frame_layout_.controls["front-door/identity"] = item_rectangle();
    if (ImGui::BeginPopup("identity-details")) {
      ImGui::TextUnformatted(
          "This computer signs your drips and changes with this identity.");
      identifier_detail("Identity", first_run_.session()->actor);
      ImGui::TextDisabled("Key custody: %s",
                          first_run_.session()->custody.c_str());
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Signs your drips and changes");
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget")) {
      first_run_.sign_out();
      persist_first_run();
    }
  } else {
    ImGui::BeginDisabled(identity_operation_.has_value());
    if (ImGui::Button(identity_operation_ ? "Reading your identity..."
                                          : "Your identity")) {
      begin_sign_in();
    }
    frame_layout_.controls["front-door/identity"] = item_rectangle();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("The key this computer signs your work with");
  }
  if (!identity_error_.empty()) {
    draw_user_error({.kind = FailureKind::local,
                     .summary = "Your identity could not be read",
                     .next_step = "Try again. liquid keeps the key in this "
                                  "computer's credential store.",
                     .details = without_absolute_paths(identity_error_)},
                    "identity-failure");
  }

  ImGui::Spacing();
  type_heading("Recent Repositories", "welcome/recent-heading");
  if (first_run_.recent().empty()) {
    ImGui::TextDisabled("Repositories you open will appear here.");
  } else if (ImGui::BeginTable("recent-repositories", 4,
                               ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Repository", ImGuiTableColumnFlags_WidthStretch,
                            1.4F);
    ImGui::TableSetupColumn("Last change", ImGuiTableColumnFlags_WidthStretch,
                            2.2F);
    ImGui::TableSetupColumn("Branch", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 86.0F);
    table_headers(effective_palette(applied_dark_theme_, applied_high_contrast_),
                  frame_layout_, "recent-repositories");
    for (const auto &recent : first_run_.recent()) {
      ImGui::PushID(recent.path.string().c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      if (ImGui::Selectable(recent.name.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        begin_open_repository(recent.path);
      }
      ImGui::TextDisabled("%s", recent.path.string().c_str());
      ImGui::TableSetColumnIndex(1);
      if (recent.last_change.starts_with("blake3:")) {
        if (ImGui::SmallButton("Change details")) {
          ImGui::OpenPopup("recent-change");
        }
        if (ImGui::BeginPopup("recent-change")) {
          identifier_detail("Change", recent.last_change);
          ImGui::EndPopup();
        }
      } else {
        detail_text(recent.last_change);
      }
      ImGui::TextDisabled("%s", recent.last_changed_at.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(recent.branch.c_str());
      ImGui::TableSetColumnIndex(3);
      if (ImGui::SmallButton("New window")) {
        open_new_window(recent.path);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
  draw_clone_dialog();
  draw_new_repository_dialog();
}

void App::draw_status_bar() {
  const auto graph =
      editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
  if (!ImGui::BeginTable("status", 3, ImGuiTableFlags_SizingStretchProp)) {
    return;
  }
  ImGui::TableSetupColumn("Repository", ImGuiTableColumnFlags_WidthStretch,
                          1.0F);
  ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 1.4F);
  ImGui::TableSetupColumn("Position and details",
                          ImGuiTableColumnFlags_WidthFixed, 190.0F);
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  const auto name = repository_ ? repository_->name() : std::string{"liquify"};
  if (ImGui::SmallButton(name.c_str()) && repository_) {
    first_run_.return_to_welcome();
  }
  if (repository_ && !repository_->branch().empty()) {
    ImGui::SameLine();
    ImGui::TextUnformatted(std::string(repository_->branch()).c_str());
  }
  ImGui::TableSetColumnIndex(1);
  auto state = std::string{};
  if (editor_ && !editor_->error().empty()) {
    state = editor_->error();
  } else if (graph.build_running) {
    state = "Building…";
  } else if (sync_was_running_) {
    state = "Syncing…";
  } else if (hydration_was_running_) {
    state = "Loading files…";
  } else if (const auto failure = status_failure(graph)) {
    const auto label =
        failure->summary + " · Open " +
        std::string(panels_.descriptor(failure->panel).title) +
        "##status-failure";
    const auto palette =
        effective_palette(applied_dark_theme_, applied_high_contrast_);
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_color(palette.danger));
    if (ImGui::SmallButton(label.c_str())) {
      panels_.focus(failure->panel);
      if (panels_.layout(failure->panel).placement == PanelPlacement::sidebar) {
        shell_state_.sidebar_visible = true;
      } else {
        shell_state_.bottom_panel_visible = true;
      }
      persist_shell_state();
    }
    ImGui::PopStyleColor();
    frame_layout_.controls["status/failure"] = item_rectangle();
  } else if (!editor_status_.empty()) {
    state = editor_status_;
  } else if (repository_ && repository_->offline() &&
             repository_->pending_uploads() > 0) {
    state = "Offline · saved changes waiting to sync";
  }
  if (!state.empty()) {
    state = without_absolute_paths(state, repository_path());
    auto visible = compact_identifiers(state);
    const auto width = ImGui::GetContentRegionAvail().x;
    while (visible.size() > 4 &&
           ImGui::CalcTextSize(visible.c_str()).x > width) {
      // Remove a complete UTF-8 code point before appending the ellipsis.
      auto last = visible.size() - 1;
      while (last > 0 &&
             (static_cast<unsigned char>(visible[last]) & 0xc0) == 0x80) {
        --last;
      }
      visible.resize(last);
    }
    ImGui::TextUnformatted(visible.c_str());
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", state.c_str());
    }
  } else if (multiplayer_) {
    const auto others = std::ranges::count_if(
        multiplayer_->presence, [this](const auto &value) {
          return value.session != multiplayer_session_;
        });
    if (others > 0) {
      const auto label = std::to_string(others) + " collaborating";
      if (ImGui::SmallButton(label.c_str())) {
        panels_.focus(PanelId::multiplayer);
        shell_state_.sidebar_visible = true;
        persist_shell_state();
      }
    }
  }
  ImGui::TableSetColumnIndex(2);
  if (editor_) {
    const auto cursor = editor_->cursor();
    ImGui::TextDisabled("Ln %zu:%zu", cursor.line + 1, cursor.byte + 1);
    ImGui::SameLine();
  }
  if (ImGui::SmallButton("Details")) {
    ImGui::OpenPopup("status-details");
  }
  if (ImGui::BeginPopup("status-details")) {
    if (ImGui::MenuItem("Repository diagnostics")) {
      repository_details_open_ = true;
      panels_.focus(PanelId::diagnostics);
      shell_state_.bottom_panel_visible = true;
      persist_shell_state();
    }
    if (ImGui::MenuItem("Cycle theme")) {
      cycle_theme();
    }
    if (editor_) {
      ImGui::TextDisabled(
          "%zu cursors | %s:%zu | %s", editor_->cursors().size(),
          editor_->preferences().indent_style == IndentStyle::tabs ? "Tabs"
                                                                   : "Spaces",
          editor_->preferences().indent_size,
          editor_->preferences().soft_wrap ? "Wrap" : "No wrap");
    }
    if (const auto *notification = panels_.latest_notification()) {
      if (ImGui::MenuItem("Show latest notification")) {
        panels_.focus(notification->panel);
        shell_state_.sidebar_visible = true;
        shell_state_.bottom_panel_visible = true;
      }
      if (notification->active && notification->cancellable &&
          ImGui::MenuItem("Cancel operation")) {
        static_cast<void>(panels_.request_cancel(notification->id));
      } else if (!notification->active &&
                 ImGui::MenuItem("Dismiss notification")) {
        panels_.dismiss_notification(notification->id);
      }
    }
    ImGui::EndPopup();
  }
  ImGui::EndTable();
}

void App::draw_contents() {
  const auto status_bar_height =
      ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0F;
  const auto available = ImGui::GetContentRegionAvail();
  const auto *screenshot =
      options_.screenshot_state
          ? find_screenshot_state(*options_.screenshot_state)
          : nullptr;
  const auto screenshot_workspace =
      screenshot != nullptr && screenshot->view != ScreenshotView::welcome &&
      screenshot->view != ScreenshotView::repository_unreadable &&
      screenshot->view != ScreenshotView::lake_offline;
  const auto show_first_run =
      !options_.smoke && !screenshot_workspace &&
      first_run_.phase() != RepositoryLifecyclePhase::open;
  if (show_first_run) {
    const auto content_height = std::max(available.y - status_bar_height -
                                             ImGui::GetStyle().ItemSpacing.y,
                                         1.0F);
    ImGui::BeginChild("first-run", ImVec2(available.x, content_height));
    draw_welcome();
    ImGui::EndChild();
    ImGui::BeginChild("status-bar", ImVec2(available.x, status_bar_height),
                      ImGuiChildFlags_Border);
    draw_status_bar();
    ImGui::EndChild();
    return;
  }
  const auto center_height = std::max(
      available.y - status_bar_height - ImGui::GetStyle().ItemSpacing.y, 1.0F);
  ImGui::BeginChild("activity-bar", ImVec2(activity_bar_width, center_height),
                    ImGuiChildFlags_Border);
  draw_activity_bar();
  ImGui::EndChild();
  ImGui::SameLine(0.0F, 0.0F);

  if (shell_state_.sidebar_visible) {
    ImGui::BeginChild("sidebar",
                      ImVec2(shell_state_.sidebar_width, center_height),
                      ImGuiChildFlags_Border);
    draw_sidebar();
    ImGui::EndChild();
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::InvisibleButton("sidebar-divider",
                           ImVec2(divider_size, center_height));
    if (ImGui::IsItemActive()) {
      shell_state_.sidebar_width =
          std::clamp(shell_state_.sidebar_width + ImGui::GetIO().MouseDelta.x,
                     180.0F, 520.0F);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      persist_shell_state();
    }
    ImGui::SameLine(0.0F, 0.0F);
  }

  const auto workspace_width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
  ImGui::BeginChild("workspace", ImVec2(workspace_width, center_height));
  const auto workspace = ImGui::GetContentRegionAvail();
  const auto maximum_panel = std::max(workspace.y - 160.0F, 120.0F);
  shell_state_.bottom_panel_height =
      std::clamp(shell_state_.bottom_panel_height, 120.0F, maximum_panel);
  const auto bottom_height = shell_state_.bottom_panel_visible
                                 ? shell_state_.bottom_panel_height
                                 : 0.0F;
  const auto bottom_spacing = shell_state_.bottom_panel_visible
                                  ? ImGui::GetStyle().ItemSpacing.y * 2.0F
                                  : 0.0F;
  const auto editor_height =
      std::max(workspace.y - bottom_height -
                   (shell_state_.bottom_panel_visible ? divider_size : 0.0F) -
                   bottom_spacing,
               1.0F);
  ImGui::BeginChild("editor-area", ImVec2(workspace.x, editor_height));
  draw_editor_area();
  ImGui::EndChild();
  if (shell_state_.bottom_panel_visible) {
    ImGui::InvisibleButton("bottom-divider", ImVec2(workspace.x, divider_size));
    if (ImGui::IsItemActive()) {
      shell_state_.bottom_panel_height = std::clamp(
          shell_state_.bottom_panel_height - ImGui::GetIO().MouseDelta.y,
          120.0F, maximum_panel);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      persist_shell_state();
    }
    ImGui::BeginChild("bottom-panel",
                      ImVec2(workspace.x, shell_state_.bottom_panel_height),
                      ImGuiChildFlags_Border);
    draw_bottom_panel();
    ImGui::EndChild();
  }
  ImGui::EndChild();

  ImGui::BeginChild("status-bar", ImVec2(available.x, status_bar_height),
                    ImGuiChildFlags_Border);
  draw_status_bar();
  ImGui::EndChild();
}

void App::render_editor(InputRectangle clip_bounds) {
  if (!editor_visible_ || !editor_ || editor_width_ <= 0.0F ||
      editor_height_ <= 0.0F) {
    return;
  }
  const auto maximum_scroll = editor_->line_count() > visible_lines_
                                  ? editor_->line_count() - visible_lines_
                                  : 0;
  scroll_line_ = std::min(scroll_line_, maximum_scroll);

  const SDL_Rect clip{
      static_cast<int>(std::ceil(clip_bounds.x)),
      static_cast<int>(std::ceil(clip_bounds.y)),
      std::max(0, static_cast<int>(std::floor(clip_bounds.width))),
      std::max(0, static_cast<int>(std::floor(clip_bounds.height)))};
  SDL_SetRenderClipRect(renderer_, &clip);
  const auto graph_snapshot =
      editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
  auto comment_lines = std::vector<std::size_t>{};
  if (review_) {
    const auto review_snapshot = review_->snapshot();
    for (const auto &comment : review_snapshot.review.comments) {
      if (comment.state == ReviewAnchorState::deleted) {
        continue;
      }
      if (review_diff_open_) {
        const auto found = std::ranges::find_if(
            review_diff_lines_, [&comment](const auto &line) {
              return line.path == comment.value.anchor.path && line.new_line &&
                     *line.new_line == comment.value.anchor.line;
            });
        if (found != review_diff_lines_.end()) {
          comment_lines.push_back(
              static_cast<std::size_t>(found - review_diff_lines_.begin()));
        }
      } else if (comment.value.anchor.path == editor_->display_path() &&
                 comment.value.anchor.line > 0) {
        comment_lines.push_back(
            static_cast<std::size_t>(comment.value.anchor.line - 1));
      }
    }
  }
  const auto review_lines =
      review_diff_open_ ? std::span<const ReviewDiffLine>{review_diff_lines_}
                        : std::span<const ReviewDiffLine>{};
  auto visible_diagnostics = graph_snapshot.diagnostics;
  visible_diagnostics.insert(visible_diagnostics.end(),
                             graph_snapshot.build_diagnostics.begin(),
                             graph_snapshot.build_diagnostics.end());
  auto *editor_font = ImGui::GetIO().Fonts->Fonts[editor_font_index_];
  const auto palette =
      effective_palette(applied_dark_theme_, applied_high_contrast_);
  const auto demo_syntax = screenshot_mode()
                               ? screenshot_syntax(editor_->text())
                               : std::vector<SyntaxSpan>{};
  auto visible_folds = graph_snapshot.folds;
  auto visible_attribution = graph_snapshot.attribution;
  auto visible_presence = graph_snapshot.presence;
  const auto *screenshot =
      options_.screenshot_state
          ? find_screenshot_state(*options_.screenshot_state)
          : nullptr;
  if (screenshot != nullptr &&
      screenshot->view == ScreenshotView::editor_view) {
    visible_diagnostics.push_back({{"src/app.cpp", {134, 144, 6, 11, 6, 21}},
                                   DiagnosticSeverity::warning,
                                   "repository argument is displayed"});
    visible_folds.push_back({3, 9, "namespace_definition"});
    visible_attribution.push_back({5, "human", "change-32", false});
    visible_attribution.push_back({6, "agent", "change-32", true});
    visible_presence.push_back({"reviewer", "src/app.cpp",
                                SourceRange{144, 154, 6, 21, 6, 31},
                                liquid::presence_activity::editing});
  }
  const auto syntax = screenshot_mode()
                          ? std::span<const SyntaxSpan>{demo_syntax}
                          : std::span<const SyntaxSpan>{graph_snapshot.syntax};
  visible_lines_ = text_layer_->render(
      renderer_, *editor_, *layouts_, *editor_font, palette,
      visible_diagnostics, syntax, visible_folds, visible_attribution,
      visible_presence, graph_snapshot.drive,
      screenshot_mode() ? 1 : graph_snapshot.syntax_generation, editor_x_,
      editor_y_, editor_width_, editor_height_, scroll_line_, line_height_,
      cell_width_, horizontal_scroll_, review_lines, comment_lines,
      settings_.editor.whitespace);
  frame_layout_.editor_text =
      text_layer_->visible_text(clip_bounds, line_height_);
}

void App::frame(bool present) {
  poll_repository_lifecycle();
  poll_sign_in();
  poll_editor_graph();
  poll_review();
  poll_plugins();
  sync_panel_system();
  const auto next_reduced_motion = system_reduced_motion();
  if (next_reduced_motion != reduced_motion_) {
    reduced_motion_ = next_reduced_motion;
    apply_theme();
  }
  if (applied_dark_theme_ != dark_theme_active() ||
      applied_high_contrast_ != high_contrast_active()) {
    apply_theme();
  }
  update_window_title();
  poll_multiplayer();
  ImGui_ImplSDL3_NewFrame();
  const auto framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale;
  const auto density =
      std::max({framebuffer_scale.x, framebuffer_scale.y, 1.0F});
  if (fonts_rebuild_requested_ || density != display_scale_) {
    ImGui_ImplSDLRenderer3_DestroyFontsTexture();
    display_scale_ = density;
    load_font_atlas(display_scale_);
    fonts_rebuild_requested_ = false;
    if (!upload_font_atlas()) {
      return;
    }
  }
  ImGui_ImplSDLRenderer3_NewFrame();
  if (screenshot_mode()) {
    ImGui::GetIO().DeltaTime = 1.0F;
  }
  ImGui::NewFrame();
  editor_visible_ = false;
  frame_layout_ = {};
  frame_layout_.drawable_scale = {framebuffer_scale.x, framebuffer_scale.y};
  frame_layout_.controls["type/body"] =
      InputRectangle{0.0F, 0.0F, 0.0F, ImGui::GetTextLineHeight()};

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("liquify", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);
  const auto window_position = ImGui::GetWindowPos();
  const auto window_size = ImGui::GetWindowSize();
  frame_layout_.window = {window_position.x, window_position.y, window_size.x,
                          window_size.y};
  draw_contents();
  ImGui::End();
  draw_floating_panels();
  draw_command_palette();
  draw_cut_composer();
  draw_set_lake_dialog();
  draw_request_review_dialog();
  draw_build_setup_dialog();
  draw_navigation_popups();
  draw_repository_action_popup();
  draw_navigation_results();
  draw_settings();
  draw_review_comment_popup();
  draw_structural_editing();

  ImGui::Render();
  const auto palette =
      effective_palette(applied_dark_theme_, applied_high_contrast_);
  frame_layout_.canvas = pixel_color(imgui_color(palette.canvas));
  frame_layout_.surface = pixel_color(imgui_color(palette.surface));
  frame_layout_.selected = pixel_color(imgui_color(palette.selected));
  SDL_SetRenderDrawColor(
      renderer_, static_cast<std::uint8_t>(palette.canvas.red * 255.0F),
      static_cast<std::uint8_t>(palette.canvas.green * 255.0F),
      static_cast<std::uint8_t>(palette.canvas.blue * 255.0F),
      static_cast<std::uint8_t>(palette.canvas.alpha * 255.0F));
  SDL_RenderClear(renderer_);
  // All vertices, clipping and input are logical; SDL applies the drawable
  // ratio once.
  if (!SDL_SetRenderScale(renderer_, framebuffer_scale.x,
                          framebuffer_scale.y)) {
    fail_rendering("Could not set the window drawable scale: " +
                   std::string(SDL_GetError()));
    return;
  }
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
  if (screenshot_mode()) {
    ++screenshot_frames_rendered_;
    if (screenshot_frames_rendered_ >= 2) {
      capture_screenshot();
    }
  }
  if (present) {
    SDL_RenderPresent(renderer_);
  }
}

void App::prepare_smoke_frame(int frame) {
  if (!options_.smoke || !editor_) {
    return;
  }
  const auto denominator = std::max(options_.max_frames - 1, 1);
  const auto last_line = editor_->line_count() - 1;
  const auto line = last_line * static_cast<std::size_t>(frame) /
                    static_cast<std::size_t>(denominator);
  scroll_line_ = line;
  editor_->set_cursor({line, editor_->line(line).size()});
  typing_started_ = std::chrono::steady_clock::now();
  const auto change = editor_->insert_text(".");
  if (change) {
    apply_change(change->first_line, change->erased_lines,
                 change->inserted_lines);
  }
}

void App::print_smoke_metrics() const {
  const auto mean_frame = average(frame_times_ms_);
  const auto mean_typing = average(typing_times_ms_);
  const auto refresh = mean_frame > 0.0 ? 1000.0 / mean_frame : 0.0;
  std::printf("smoke: lines=50000 frames=%zu visible_lines=%zu ",
              frame_times_ms_.size(), visible_lines_);
  std::printf(
      "avg_frame_ms=%.3f p95_frame_ms=%.3f achieved_hz=%.1f display_hz=%.1f ",
      mean_frame, percentile(frame_times_ms_, 0.95), refresh,
      display_refresh_hz_);
  std::printf("avg_typing_ms=%.3f p95_typing_ms=%.3f\n", mean_typing,
              percentile(typing_times_ms_, 0.95));
}

namespace {
// Distinct from the native menu commands above, which are small positive ints.
constexpr std::int32_t repaint_wake_code = 0x5751; // "WQ"
} // namespace

void App::request_repaint() {
  // Already pending: the loop has a frame coming, so skip the syscall.
  if (repaint_requested_.exchange(true)) {
    return;
  }
  // Wake a blocked SDL_WaitEventTimeout. SDL_PushEvent is thread-safe, which is
  // what lets a producer on another thread ask for a frame.
  auto event = SDL_Event{};
  event.type = SDL_EVENT_USER;
  event.user.code = repaint_wake_code;
  SDL_PushEvent(&event);
}

auto App::interface_is_busy() const -> bool {
  const auto &io = ImGui::GetIO();
  // Only genuine activity counts. Deliberately NOT io.WantCaptureMouse /
  // WantCaptureKeyboard / WantTextInput: those say where input would be routed,
  // not that anything is happening, and they are true whenever the pointer is
  // over the interface or a text field has focus — which is almost always. Using
  // them here is what kept the loop at the display rate.
  //
  // A focused caret still blinks: max_idle_wait wakes the loop several times a
  // second, which is enough for the blink and costs almost nothing.
  return ImGui::IsAnyItemActive() || ImGui::IsAnyMouseDown() ||
         io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F ||
         io.MouseWheel != 0.0F || io.MouseWheelH != 0.0F ||
         command_palette_open_;
}

auto App::async_work_in_flight() const -> bool {
  // Every future polled from the frame path. While one is outstanding the loop
  // keeps frame cadence so the result is noticed as promptly as before.
  return (repository_operation_ && repository_operation_->valid()) ||
         (identity_operation_ && identity_operation_->valid()) ||
         (cut_preview_ && cut_preview_->valid()) ||
         (cut_operation_ && cut_operation_->valid()) ||
         (build_setup_operation_ && build_setup_operation_->valid()) ||
         multiplayer_refresh_.valid();
}

auto App::until_next_scheduled_refresh() const
    -> std::chrono::steady_clock::duration {
  const auto now = std::chrono::steady_clock::now();
  auto nearest = std::chrono::steady_clock::duration::max();
  const auto consider = [&](std::chrono::steady_clock::time_point deadline) {
    // A default-constructed deadline is one that has never been scheduled.
    if (deadline == std::chrono::steady_clock::time_point{}) {
      return;
    }
    nearest = std::min(nearest, deadline - now);
  };
  consider(next_status_refresh_);
  consider(next_review_refresh_);
  consider(next_plugin_refresh_);
  consider(next_multiplayer_refresh_);
  return nearest;
}

auto App::redraw_inputs() const -> RedrawInputs {
  return {.repaint_requested = repaint_requested_.load(),
          .interacting = interface_is_busy(),
          .async_in_flight = async_work_in_flight(),
          .until_scheduled = until_next_scheduled_refresh()};
}

int App::run() {
  if (!initialized_) {
    return 1;
  }
  int frames = 0;
  // Smoke and screenshot runs render a fixed number of frames as fast as they
  // can; they are measurements, not a window someone is sitting in front of, so
  // they keep the unconditional loop.
  const bool continuous = options_.smoke || options_.max_frames > 0;
  while (!quit_) {
    SDL_Event event;
    if (!continuous) {
      // Wait for a cause instead of spinning. Every wake produces a frame below,
      // so sleeping here cannot swallow a state change; the only decision is how
      // long it is safe to wait. See docs/ui/redraw.md.
      const auto wait = idle_wait(redraw_inputs());
      if (wait > std::chrono::milliseconds::zero()) {
        // A null event waits without dequeuing, so whatever arrives is still
        // there for the single drain below. Taking it here instead would need it
        // pushed back, which reorders the queue.
        SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(wait.count()));
      }
    }
    repaint_requested_.store(false);
    while (SDL_PollEvent(&event)) {
      // Any event is a reason to draw.
      repaint_requested_.store(true);
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        request_quit();
      }
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window_)) {
        request_quit();
      }
      if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED &&
          event.window.windowID == SDL_GetWindowID(window_)) {
        const auto next_scale =
            std::max(SDL_GetWindowDisplayScale(window_), 1.0F);
        if (next_scale != display_scale_) {
          ImGui_ImplSDLRenderer3_DestroyFontsTexture();
          display_scale_ = next_scale;
          load_font_atlas(display_scale_);
          ImGui_ImplSDLRenderer3_CreateFontsTexture();
        }
      }
      if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
          event.window.windowID == SDL_GetWindowID(window_)) {
        input_.cancel_composition();
        input_.clear_drop_target();
      }
      if (event.type == SDL_EVENT_TEXT_EDITING) {
        handle_composition(event.edit.text, event.edit.start,
                           event.edit.length);
      }
      if (event.type == SDL_EVENT_TEXT_INPUT) {
        handle_text(input_.commit_text(event.text.text));
      }
      if (event.type == SDL_EVENT_KEY_DOWN) {
        handle_key(event.key.key, static_cast<std::uint16_t>(event.key.mod));
      }
      if (event.type == SDL_EVENT_DROP_POSITION) {
        static_cast<void>(input_.update_drop_target(
            {event.drop.x, event.drop.y}, repository_drop_rectangle_,
            editor_drop_rectangle_));
      }
      if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
        handle_file_drop(event.drop.data, {event.drop.x, event.drop.y});
      }
      if (event.type == SDL_EVENT_DROP_COMPLETE) {
        input_.clear_drop_target();
      }
#if defined(__APPLE__) && defined(__OBJC__)
      if (event.type == SDL_EVENT_USER) {
        switch (event.user.code) {
        case menu_close:
          close_editor();
          break;
        case menu_quit:
          request_quit();
          break;
        case menu_save:
          save_editor();
          break;
        case menu_palette:
          command_palette_open_ = true;
          break;
        case menu_sidebar:
          shell_state_.sidebar_visible = !shell_state_.sidebar_visible;
          persist_shell_state();
          break;
        case menu_panel:
          shell_state_.bottom_panel_visible =
              !shell_state_.bottom_panel_visible;
          persist_shell_state();
          break;
        case menu_split:
          shell_state_.editor_split = !shell_state_.editor_split;
          persist_shell_state();
          break;
        case menu_theme:
          cycle_theme();
          break;
        case menu_contrast:
          toggle_high_contrast();
          break;
        case menu_copy:
          copy_editor_selection(false);
          break;
        case menu_cut:
          copy_editor_selection(true);
          break;
        case menu_paste:
          paste_editor_clipboard();
          break;
        case menu_definition:
          request_definition();
          break;
        case menu_hover:
          request_hover();
          break;
        case menu_build:
          request_build();
          break;
        case menu_tests:
          request_tests();
          break;
        case menu_push:
          request_push();
          break;
        case menu_cut_change:
          static_cast<void>(invoke_command("change.cut"));
          break;
        case menu_next_hunk:
          navigate_hunk(true);
          break;
        case menu_previous_hunk:
          navigate_hunk(false);
          break;
        case menu_next_comment:
          navigate_comment();
          break;
        case menu_expand_selection:
          expand_structural_selection();
          break;
        case menu_shrink_selection:
          shrink_structural_selection();
          break;
        case menu_quick_open:
          invoke_navigation_action(NavigationAction::quick_open);
          break;
        case menu_symbol_search:
          invoke_navigation_action(NavigationAction::symbol_search);
          break;
        case menu_references:
          invoke_navigation_action(NavigationAction::find_references);
          break;
        case menu_call_hierarchy:
          invoke_navigation_action(NavigationAction::call_hierarchy);
          break;
        case menu_open_recent:
          invoke_navigation_action(NavigationAction::open_recent);
          break;
        case menu_back:
          invoke_navigation_action(NavigationAction::back);
          break;
        case menu_forward:
          invoke_navigation_action(NavigationAction::forward);
          break;
        default:
          break;
        }
      }
#endif
      if (quit_) {
        break;
      }
    }
    if (quit_) {
      break;
    }

    prepare_smoke_frame(frames);
    const auto frame_started = std::chrono::steady_clock::now();
    frame();
    const auto presented = std::chrono::steady_clock::now();
    const auto frame_time =
        std::chrono::duration<double, std::milli>(presented - frame_started);
    if (options_.smoke) {
      frame_times_ms_.push_back(frame_time.count());
    }
    if (typing_started_) {
      const auto typing_time = std::chrono::duration<double, std::milli>(
          presented - *typing_started_);
      typing_times_ms_.push_back(typing_time.count());
      typing_started_.reset();
    }

    ++frames;
    if (options_.max_frames > 0 && frames >= options_.max_frames) {
      break;
    }
  }
  if (options_.smoke) {
    print_smoke_metrics();
  }
  return screenshot_failed_ || rendering_failed_ ? 1 : 0;
}

void App::shutdown() {
  if (!initialized_) {
    return;
  }
  persist_shell_state();
  persist_first_run();
  SDL_StopTextInput(window_);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
  renderer_ = nullptr;
  window_ = nullptr;
  initialized_ = false;
}

#ifdef LIQUIFY_NATIVE_BUFFER_REGRESSION
void NativeBufferRegression::text(std::string_view value) {
  auto event = SDL_Event{};
  event.type = SDL_EVENT_TEXT_INPUT;
  event.text.text = value.data();
  SDL_PushEvent(&event);
}

void NativeBufferRegression::key(char value, bool shift) {
  auto event = SDL_Event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = static_cast<SDL_Keycode>(value);
  event.key.mod = SDL_KMOD_GUI;
  if (shift) {
    event.key.mod |= SDL_KMOD_SHIFT;
  }
  SDL_PushEvent(&event);
}

void NativeBufferRegression::close_window(App &app) {
  auto event = SDL_Event{};
  event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  event.window.windowID = SDL_GetWindowID(app.window_);
  SDL_PushEvent(&event);
}

void NativeBufferRegression::describe_window(App &app) {
  std::printf(
      "Native window: driver=%s scale=%.1f visible=%s\n",
      SDL_GetCurrentVideoDriver(), SDL_GetWindowDisplayScale(app.window_),
      (SDL_GetWindowFlags(app.window_) & SDL_WINDOW_HIDDEN) ? "no" : "yes");
}

void NativeBufferRegression::answer_dialog(std::string_view label) {
  const auto separator = label.find('|');
  const auto remaining = separator == std::string_view::npos
                             ? std::string{}
                             : std::string(label.substr(separator + 1));
  label = label.substr(0, separator);
  auto *title = [[NSString alloc] initWithBytes:label.data()
                                         length:label.size()
                                       encoding:NSUTF8StringEncoding];
  // Click the actual native alert button after the modal run loop starts.
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC),
      dispatch_get_main_queue(), ^{
        auto *modal = [NSApp modalWindow];
        auto *pending = [NSMutableArray array];
        if (modal != nil) {
          [pending addObject:[modal contentView]];
          if ([modal attachedSheet] != nil) {
            [pending addObject:[[modal attachedSheet] contentView]];
          }
        }
        while ([pending count] != 0) {
          NSView *view = [pending lastObject];
          [pending removeLastObject];
          if ([view isKindOfClass:[NSButton class]] &&
              [[(NSButton *)view title] isEqualToString:title]) {
            std::printf("Native dialog: clicked %s\n", [title UTF8String]);
            if (!remaining.empty()) {
              answer_dialog(remaining);
            }
            [(NSButton *)view performClick:nil];
            [title release];
            return;
          }
          [pending addObjectsFromArray:[view subviews]];
        }
        std::fprintf(stderr, "FAIL: native dialog button %s was not found\n",
                     [title UTF8String]);
        [title release];
        std::exit(1);
      });
}
#endif

#ifdef LIQUIFY_NATIVE_WINDOW_TESTS

// Acceptance adapters send input through App::run, the production event loop.
int NativeAcceptance::launch(App &app, int density) {
  if (!app.init()) {
    return 1;
  }
  test::require_property(std::string_view(SDL_GetCurrentVideoDriver()) !=
                             "dummy",
                         "native acceptance requires a desktop video driver");
  test::require_property(std::string_view(SDL_GetRendererName(app.renderer_)) !=
                             "software",
                         "native acceptance requires a GPU renderer");
  SDL_RaiseWindow(app.window_);
  pulse(app);
  auto scale = app.frame_layout().drawable_scale;
  if (scale.x != density || scale.y != density) {
    int count = 0;
    auto *displays = SDL_GetDisplays(&count);
    test::require_property(displays != nullptr,
                           "native displays are unavailable");
    for (int index = 0; index < count; ++index) {
      SDL_SetWindowPosition(app.window_,
                            SDL_WINDOWPOS_CENTERED_DISPLAY(displays[index]),
                            SDL_WINDOWPOS_CENTERED_DISPLAY(displays[index]));
      SDL_SyncWindow(app.window_);
      pulse(app);
      scale = app.frame_layout().drawable_scale;
      std::printf(
          "native display: name=%s drawable_density=%.1fx%.1f requested=%d\n",
          SDL_GetDisplayName(displays[index]), scale.x, scale.y, density);
      if (scale.x == density && scale.y == density) {
        break;
      }
    }
    SDL_free(displays);
  }
  if (scale.x != density || scale.y != density) {
    std::printf(
        "SKIP: unmodified native launch cannot reach drawable density %d; "
        "attached displays are density 2.0x2.0\n",
        density);
    return 77;
  }
  std::printf(
      "native window: driver=%s renderer=%s density=%.1fx%.1f hidden=%d\n",
      SDL_GetCurrentVideoDriver(), SDL_GetRendererName(app.renderer_), scale.x,
      scale.y, (SDL_GetWindowFlags(app.window_) & SDL_WINDOW_HIDDEN) != 0);
  return 0;
}

void NativeAcceptance::pulse(App &app) {
  app.options_.max_frames = 4;
  test::require_property(app.run() == 0,
                         "native application event loop failed");
}

void NativeAcceptance::click(App &app, InputRectangle rectangle) {
  test::require_bounds(rectangle, app.frame_layout().window, "pointer target");
  const auto x = rectangle.x + rectangle.width * 0.5F;
  const auto y = rectangle.y + rectangle.height * 0.5F;
  SDL_WarpMouseInWindow(app.window_, x, y);
  auto event = SDL_Event{};
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.windowID = SDL_GetWindowID(app.window_);
  event.motion.x = x;
  event.motion.y = y;
  SDL_PushEvent(&event);
  pulse(app);
  // ImGui decides what is hovered from the previous frame; a loaded machine can deliver the
  // motion late, so the press follows a second settled pulse, as a person's would.
  pulse(app);
  event = {};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.windowID = SDL_GetWindowID(app.window_);
  event.button.button = SDL_BUTTON_LEFT;
  event.button.down = true;
  event.button.x = x;
  event.button.y = y;
  SDL_PushEvent(&event);
  pulse(app);
  event.type = SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.down = false;
  SDL_PushEvent(&event);
  pulse(app);
}

void NativeAcceptance::key(App &app, std::string_view name, bool primary,
                           bool shift) {
  const auto label = std::string(name);
  const auto code =
      name == "Enter" ? SDLK_RETURN : SDL_GetKeyFromName(label.c_str());
  test::require_property(code != SDLK_UNKNOWN, "unknown acceptance key");
  auto event = SDL_Event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.windowID = SDL_GetWindowID(app.window_);
  event.key.key = code;
  event.key.scancode = SDL_GetScancodeFromKey(code, nullptr);
  event.key.down = true;
#if defined(__APPLE__)
  event.key.mod = primary ? SDL_KMOD_GUI : SDL_KMOD_NONE;
#else
  event.key.mod = primary ? SDL_KMOD_CTRL : SDL_KMOD_NONE;
#endif
  if (shift) {
    event.key.mod |= SDL_KMOD_SHIFT;
  }
  SDL_PushEvent(&event);
  pulse(app);
  event.type = SDL_EVENT_KEY_UP;
  event.key.down = false;
  event.key.mod = SDL_KMOD_NONE;
  SDL_PushEvent(&event);
  pulse(app);
}

void NativeAcceptance::text(App &app, std::string_view value) {
  // A person types some frames after the click that focused a field; give SDL's text input
  // the same frames to activate before requiring it.
  for (int frame = 0; frame < 20 && !SDL_TextInputActive(app.window_); ++frame) {
    pulse(app);
  }
  const auto content = std::string(value);
  std::printf("native text: input_active=%d bytes=%zu\n",
              SDL_TextInputActive(app.window_), content.size());
  test::require_property(
      SDL_TextInputActive(app.window_),
      "#39: native text input is inactive after focus transition");
  auto event = SDL_Event{};
  event.type = SDL_EVENT_TEXT_INPUT;
  event.text.windowID = SDL_GetWindowID(app.window_);
  event.text.text = content.c_str();
  SDL_PushEvent(&event);
  pulse(app);
}

ScreenshotImage NativeAcceptance::capture(App &app) {
  pulse(app);
  app.frame(false);
  auto *pixels = SDL_RenderReadPixels(app.renderer_, nullptr);
  test::require_property(pixels != nullptr,
                         "native framebuffer readback failed");
  auto *rgba = SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(pixels);
  test::require_property(rgba != nullptr,
                         "native framebuffer conversion failed");
  ScreenshotImage image;
  image.width = rgba->w;
  image.height = rgba->h;
  const auto *data = static_cast<const std::uint8_t *>(rgba->pixels);
  for (int row = 0; row < rgba->h; ++row) {
    image.rgba.insert(image.rgba.end(), data + row * rgba->pitch,
                      data + row * rgba->pitch + rgba->w * 4);
  }
  SDL_DestroySurface(rgba);
  SDL_RenderPresent(app.renderer_);
  return image;
}

bool NativeAcceptance::popup(std::string_view name) {
  const auto label = std::string(name);
  const auto *window = ImGui::FindWindowByName(label.c_str());
  return window != nullptr && window->Active;
}

void NativeAcceptance::resize(App &app) {
  const auto before = app.frame_layout().window;
  test::require_property(
      SDL_SetWindowSize(app.window_, static_cast<int>(before.width * 0.8F),
                        static_cast<int>(before.height * 0.8F)),
      "native resize failed");
  SDL_SyncWindow(app.window_);
  pulse(app);
  test::require_property(app.frame_layout().window.width < before.width,
                         "native resize did not change layout");
}

void NativeAcceptance::fonts(App &app) {
  const auto &layout = app.frame_layout();
  for (const auto index : {app.ui_font_index_, app.editor_font_index_}) {
    test::require_property(
        ImGui::GetIO().Fonts->Fonts[index]->ConfigData->RasterizerDensity >=
            std::max(layout.drawable_scale.x, layout.drawable_scale.y),
        "uploaded font density is below drawable density");
  }
  std::printf(
      "fonts: uploaded shell and editor atlases match drawable density\n");
}

void NativeAcceptance::source_tab(App &app) {
  auto &bars = ImGui::GetCurrentContext()->TabBars;
  for (int index = 0; index < bars.GetMapSize(); ++index) {
    auto *bar = bars.TryGetMapData(index);
    if (bar == nullptr) {
      continue;
    }
    for (auto &tab : bar->Tabs) {
      if (std::string_view(ImGui::TabBarGetTabName(bar, &tab))
              .ends_with("###active-editor")) {
        const auto bounds =
            InputRectangle{bar->BarRect.Min.x + tab.Offset, bar->BarRect.Min.y,
                           tab.Width, bar->BarRect.GetHeight()};
        click(app, bounds);
        return;
      }
    }
  }
  throw std::runtime_error("source tab was not laid out");
}

// The display test stays here to preserve the single SDL/ImGui translation
// unit.
class NativeWindowTest {
public:
  static int run(const std::filesystem::path &output, bool high_density) {
    try {
      App app({.forced_theme = ThemePreference::light});
      require(app.init_window(high_density),
              "native window initialization failed");
      require(std::string_view(SDL_GetCurrentVideoDriver()) != "dummy",
              "native proof requires a desktop video driver");
      require(std::string_view(SDL_GetRendererName(app.renderer_)) !=
                  "software",
              "native proof requires a GPU renderer");
      app.reset_panel_layout();
      for (const auto panel :
           app.panels_.panels_at(PanelPlacement::editor_tab)) {
        app.panels_.set_visible(panel, false);
      }
      ImGui::GetIO().ConfigInputTextCursorBlink = false;
      SDL_SetWindowSize(app.window_, 1280, 800);
      SDL_SyncWindow(app.window_);
      app.shell_state_.bottom_panel_visible = false;
      app.shell_state_.editor_split = false;
      app.shell_state_.sidebar_visible = true;
      // #54: The native renderer test uses the screenshot suite's in-memory
      // source fixture. An external liquid CLI can produce a newer store
      // format than the libliquid pin, and repository loading is outside this
      // rendering regression.
      app.first_run_.repository_opened(
          {.path = "native-window-fixture", .name = "native-window-fixture"});
      auto source = screenshot_text();
      for (int line = 0; line < 100; ++line) {
        source += "// Native window rendering: " + std::string(180, 'M') + "\n";
      }
      app.transient_editor_ =
          std::make_unique<EditorBuffer>(EditorBuffer::from_text(
              "native-window-source", "src/main.cpp", source));
      app.editor_ = app.transient_editor_.get();
      app.layouts_ = std::make_unique<TextLayoutCache>();
      app.layouts_->reset(std::string(app.editor_->blob_id()),
                          app.editor_->line_count());
      app.text_layer_ = std::make_unique<RetainedTextLayer>();
      app.text_layer_->reset(app.editor_->line_count());
      app.editor_focused_ = true;
      const auto prefix = output / (high_density ? "density-2" : "density-1");
      capture(app);
      focus_source_tab(app);
      auto image = capture(app);
      save(prefix.string() + "-editor.png", image);
      const auto scale = ImGui::GetIO().DisplayFramebufferScale;
#if defined(__APPLE__)
      require(scale.x == (high_density ? 2.0F : 1.0F),
              "macOS proof requires an actual scale-2 display and a density-1 "
              "control window");
#endif
      check_geometry(app, image, "initial");
      check_hit(app);
      app.shell_state_.bottom_panel_visible = true;
      app.panels_.focus(PanelId::diagnostics);
      image = capture(app);
      save(prefix.string() + "-panel.png", image);
      check_geometry(app, image, "panel visible");
      SDL_SetWindowSize(app.window_, 1510, 930);
      SDL_SyncWindow(app.window_);
      image = capture(app);
      save(prefix.string() + "-resized.png", image);
      check_geometry(app, image, "resized");
      check_hit(app);
      app.settings_.fonts.ui_size = 16;
      app.settings_.fonts.editor_size = 16;
      app.apply_settings_live(true);
      image = capture(app);
      save(prefix.string() + "-fonts.png", image);
      check_geometry(app, image, "font rebuild");
      check_hit(app);
      check_displays(app);
      check_overlays(app, prefix);
      return 0;
    } catch (const std::exception &error) {
      std::fprintf(stderr, "native-window: FAIL %s\n", error.what());
      return 1;
    }
  }

private:
  static void require(bool condition, const std::string &message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  static ScreenshotImage capture(App &app) {
    for (int frame = 0; frame < 5; ++frame) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
      }
      app.frame(false);
      if (frame < 4) {
        SDL_RenderPresent(app.renderer_);
        SDL_Delay(20);
      }
    }
    auto *pixels = SDL_RenderReadPixels(app.renderer_, nullptr);
    require(pixels != nullptr, "native framebuffer readback failed");
    auto *rgba = SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(pixels);
    require(rgba != nullptr, "native framebuffer conversion failed");
    ScreenshotImage image;
    image.width = rgba->w;
    image.height = rgba->h;
    const auto *data = static_cast<const std::uint8_t *>(rgba->pixels);
    for (int row = 0; row < rgba->h; ++row) {
      image.rgba.insert(image.rgba.end(), data + row * rgba->pitch,
                        data + row * rgba->pitch + rgba->w * 4);
    }
    SDL_DestroySurface(rgba);
    SDL_RenderPresent(app.renderer_);
    return image;
  }

  static void save(const std::filesystem::path &path,
                   const ScreenshotImage &image) {
    const auto written = write_png(path, image);
    require(written.has_value(),
            "could not write native evidence: " + path.string());
  }

  static void check_geometry(App &app, const ScreenshotImage &image,
                             const char *stage) {
    const auto &layout = app.frame_layout();
    const auto coverage = test::require_window_coverage(image, layout);
    test::require_toolbar(image, layout);
    test::require_editor(image, layout);
    if (app.shell_state_.bottom_panel_visible) {
      test::require_panel(image, layout, PanelId::diagnostics, false);
    } else {
      const auto panel = layout.panels.find(PanelId::diagnostics);
      require(panel == layout.panels.end() || panel->second.content.height == 0,
              "hidden panel retained stale content bounds");
    }
    const auto &io = ImGui::GetIO();
    require(
        io.Fonts->Fonts[app.ui_font_index_]->ConfigData->RasterizerDensity >=
            std::max(layout.drawable_scale.x, layout.drawable_scale.y),
        "shell font atlas density is below the drawable scale");
    require(io.Fonts->Fonts[app.editor_font_index_]
                    ->ConfigData->RasterizerDensity >=
                std::max(layout.drawable_scale.x, layout.drawable_scale.y),
            "editor font atlas density is below the drawable scale");
    std::printf(
        "native-window: %s renderer=%s logical=%.0fx%.0f drawable=%ux%u "
        "scale=%.1fx%.1f coverage=%.3f toolbar=%.0f,%.0f %.0fx%.0f "
        "source=%.0f,%.0f %.0fx%.0f panel=%s\n",
        stage, SDL_GetRendererName(app.renderer_), layout.window.width,
        layout.window.height, image.width, image.height,
        layout.drawable_scale.x, layout.drawable_scale.y, coverage,
        layout.toolbar.x, layout.toolbar.y, layout.toolbar.width,
        layout.toolbar.height, layout.editor_text.ink.x,
        layout.editor_text.ink.y, layout.editor_text.ink.width,
        layout.editor_text.ink.height,
        app.shell_state_.bottom_panel_visible ? "visible" : "hidden");
  }

  static void check_hit(App &app) {
    constexpr std::size_t target_line = 15;
    constexpr auto target_column = 20.2F;
    const auto &layout =
        app.layouts_->line(target_line, app.editor_->line(target_line));
    const auto target_byte = layout.byte_for_column(
        target_column, app.editor_->line(target_line).size());
    const auto x = app.editor_x_ + editor_gutter_width + editor_padding +
                   app.cell_width_ * target_column;
    const auto y = app.editor_y_ + app.line_height_ * 15.5F;
    app.position_cursor(x, y, false);
    const auto cursor = app.editor_->cursor();
    std::printf("native-window: hit expected=%zu:%zu actual=%zu:%zu "
                "position=%.1f,%.1f "
                "scroll=%.1f\n",
                target_line, target_byte, cursor.line, cursor.byte, x, y,
                app.horizontal_scroll_);
    require(cursor.line == target_line && cursor.byte == target_byte,
            "logical mouse hit did not resolve through the displayed text "
            "layout");
  }

  static void focus_source_tab(App &app) {
    auto &bars = ImGui::GetCurrentContext()->TabBars;
    for (int index = 0; index < bars.GetMapSize(); ++index) {
      auto *bar = bars.TryGetMapData(index);
      if (bar == nullptr) {
        continue;
      }
      for (auto &tab : bar->Tabs) {
        if (!std::string_view(ImGui::TabBarGetTabName(bar, &tab))
                 .ends_with("###active-editor")) {
          continue;
        }
        const auto x = bar->BarRect.Min.x + tab.Offset + tab.Width * 0.5F;
        const auto y = bar->BarRect.GetCenter().y;
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = SDL_GetWindowID(app.window_);
        event.motion.x = x;
        event.motion.y = y;
        SDL_PushEvent(&event);
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = SDL_GetWindowID(app.window_);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = true;
        event.button.x = x;
        event.button.y = y;
        SDL_PushEvent(&event);
        capture(app);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.down = false;
        SDL_PushEvent(&event);
        capture(app);
        require(app.editor_visible_,
                "source tab click did not expose the editor");
        return;
      }
    }
    require(false, "source tab was not created");
  }

  static void check_displays(App &app) {
    int count = 0;
    auto *displays = SDL_GetDisplays(&count);
    require(displays != nullptr, "could not enumerate native displays");
    for (int index = 0; index < count; ++index) {
      SDL_SetWindowPosition(app.window_,
                            SDL_WINDOWPOS_CENTERED_DISPLAY(displays[index]),
                            SDL_WINDOWPOS_CENTERED_DISPLAY(displays[index]));
      SDL_SyncWindow(app.window_);
      const auto image = capture(app);
      check_geometry(app, image, "display move");
    }
    SDL_free(displays);
  }

  static void replace_buffer(App &app, const std::string &text) {
    app.transient_editor_ = std::make_unique<EditorBuffer>(
        EditorBuffer::from_text("native-occlusion", "src/main.cpp", text));
    app.editor_ = app.transient_editor_.get();
    app.layouts_->reset("native-occlusion", app.editor_->line_count());
    app.text_layer_->reset(app.editor_->line_count());
  }

  static void compare_overlay(const ScreenshotImage &with_source,
                              const ScreenshotImage &without_source,
                              std::string_view name, InputRectangle content) {
    const auto scale = ImGui::GetIO().DisplayFramebufferScale;
    std::size_t changed = 0;
    auto first_x = with_source.width;
    auto first_y = with_source.height;
    auto last_x = 0U;
    auto last_y = 0U;
    for (int y = static_cast<int>(content.y * scale.y);
         y < static_cast<int>((content.y + content.height) * scale.y); ++y) {
      for (int x = static_cast<int>(content.x * scale.x);
           x < static_cast<int>((content.x + content.width) * scale.x); ++x) {
        if (x < 0 || y < 0 || x >= with_source.width ||
            y >= with_source.height) {
          continue;
        }
        const auto offset = (y * with_source.width + x) * 4;
        for (int channel = 0; channel < 3; ++channel) {
          if (with_source.rgba[offset + channel] !=
              without_source.rgba[offset + channel]) {
            ++changed;
            first_x = std::min(first_x, static_cast<std::uint32_t>(x));
            first_y = std::min(first_y, static_cast<std::uint32_t>(y));
            last_x = std::max(last_x, static_cast<std::uint32_t>(x));
            last_y = std::max(last_y, static_cast<std::uint32_t>(y));
          }
        }
      }
    }
    if (changed == 0) {
      std::printf("native-window: overlay=%.*s changed_channels=0\n",
                  static_cast<int>(name.size()), name.data());
    } else {
      std::printf("native-window: overlay=%.*s changed_channels=%zu "
                  "bounds=%u,%u..%u,%u\n",
                  static_cast<int>(name.size()), name.data(), changed, first_x,
                  first_y, last_x, last_y);
    }
    require(changed == 0, "retained source paints over " + std::string(name));
  }

  static void check_overlays(App &app, const std::filesystem::path &prefix) {
    const auto source = std::string(app.editor_->text());
    app.command_palette_open_ = true;
    auto with_source = capture(app);
    save(prefix.string() + "-palette.png", with_source);
    auto *window = ImGui::FindWindowByName("Command Palette");
    require(window != nullptr && window->Active,
            "command palette did not open");
    // The antialiased outer border blends with whatever is behind the popup.
    // Compare its opaque client content using the window's current padding.
    const auto palette_rect = window->InnerRect;
    const auto padding = window->WindowPadding;
    const auto palette_content = InputRectangle{
        palette_rect.Min.x + padding.x, palette_rect.Min.y + padding.y,
        palette_rect.GetWidth() - padding.x * 2,
        palette_rect.GetHeight() - padding.y * 2};
    replace_buffer(app, "");
    auto without_source = capture(app);
    save(prefix.string() + "-palette-empty-editor.png", without_source);
    compare_overlay(with_source, without_source, "Command Palette",
                    palette_content);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    capture(app);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    capture(app);
    replace_buffer(app, source);
    app.panels_.move(PanelId::diagnostics, PanelPlacement::floating);
    app.panels_.update_floating_rect(PanelId::diagnostics,
                                     {app.editor_x_, app.editor_y_,
                                      app.editor_width_ * 0.7F,
                                      app.editor_height_ * 0.7F});
    app.panels_.focus(PanelId::diagnostics);
    with_source = capture(app);
    save(prefix.string() + "-floating.png", with_source);
    window = ImGui::FindWindowByName("Diagnostics###floating-diagnostics");
    require(window != nullptr && window->Active,
            "floating diagnostics did not open");
    const auto floating_content =
        app.frame_layout().panels.at(PanelId::diagnostics).content;
    replace_buffer(app, "");
    without_source = capture(app);
    save(prefix.string() + "-floating-empty-editor.png", without_source);
    compare_overlay(with_source, without_source, "Floating diagnostics",
                    floating_content);
  }
};

int test_native_window(const std::filesystem::path &output, bool high_density) {
  return NativeWindowTest::run(output, high_density);
}
#endif

} // namespace liquify
