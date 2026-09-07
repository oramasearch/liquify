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
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "misc/cpp/imgui_stdlib.h"
#include <SDL3/SDL.h>

#if defined(__APPLE__) && defined(__OBJC__)
#import <AppKit/AppKit.h>
#endif

#include "editor_buffer.hpp"
#include "liquid/hash.hpp"
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

#if defined(__APPLE__) && defined(__OBJC__)
enum NativeMenuCommand : std::int32_t {
  menu_save = 1,
  menu_palette,
  menu_sidebar,
  menu_panel,
  menu_split,
  menu_theme,
  menu_definition,
  menu_hover,
  menu_build,
  menu_tests,
  menu_deploy,
  menu_next_hunk,
  menu_previous_hunk,
  menu_next_comment,
  menu_expand_selection,
  menu_shrink_selection,
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
  auto *quit = [[NSMenuItem alloc] initWithTitle:@"Quit liquify"
                                          action:@selector(terminate:)
                                   keyEquivalent:@"q"];
  [application_menu addItem:quit];
  [application_root setSubmenu:application_menu];
  [main_menu addItem:application_root];

  constexpr auto command = NSEventModifierFlagCommand;
  constexpr auto command_shift =
      NSEventModifierFlagCommand | NSEventModifierFlagShift;
  add_native_menu(main_menu, @"File", {{@"Save", @"s", command, menu_save}});
  add_native_menu(main_menu, @"Edit",
                  {{@"Command Palette", @"p", command_shift, menu_palette}});
  add_native_menu(main_menu, @"Selection",
                  {{@"Expand Selection", @"", 0, menu_expand_selection},
                   {@"Shrink Selection", @"", 0, menu_shrink_selection}});
  add_native_menu(main_menu, @"View",
                  {{@"Toggle Sidebar", @"b", command, menu_sidebar},
                   {@"Toggle Bottom Panel", @"j", command, menu_panel},
                   {@"Toggle Editor Split", @"\\", command, menu_split},
                   {@"Cycle Color Theme", @"t", command_shift, menu_theme}});
  add_native_menu(main_menu, @"Go",
                  {{@"Go to Definition", @"", 0, menu_definition},
                   {@"Show Hover", @"", 0, menu_hover}});
  add_native_menu(main_menu, @"Run",
                  {{@"Build", @"", 0, menu_build},
                   {@"Test", @"", 0, menu_tests},
                   {@"Deploy", @"", 0, menu_deploy}});
  add_native_menu(main_menu, @"Review",
                  {{@"Next Hunk", @"", 0, menu_next_hunk},
                   {@"Previous Hunk", @"", 0, menu_previous_hunk},
                   {@"Next Comment", @"", 0, menu_next_comment}});
  [NSApp setMainMenu:main_menu];
}

struct RegistryMenuEntry {
  std::string group;
  std::string title;
  std::string key;
  std::uint8_t modifiers{};
  std::int32_t command{};
};

static auto registry_key_equivalent(std::string_view key) -> NSString * {
  if (key.size() == 1) {
    const auto character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(key[0])));
    return [NSString stringWithFormat:@"%c", character];
  }
  if (key == "Backslash") {
    return @"\\";
  }
  if (key == "Space") {
    return @" ";
  }
  if (key == "Enter") {
    return @"\r";
  }
  auto function = unichar{};
  if (key == "Up") {
    function = NSUpArrowFunctionKey;
  } else if (key == "Down") {
    function = NSDownArrowFunctionKey;
  } else if (key == "Left") {
    function = NSLeftArrowFunctionKey;
  } else if (key == "Right") {
    function = NSRightArrowFunctionKey;
  } else if (key == "Home") {
    function = NSHomeFunctionKey;
  } else if (key == "End") {
    function = NSEndFunctionKey;
  } else if (key == "PageUp") {
    function = NSPageUpFunctionKey;
  } else if (key == "PageDown") {
    function = NSPageDownFunctionKey;
  } else if (key == "Delete" || key == "Backspace") {
    function = NSDeleteFunctionKey;
  } else if (key.starts_with("F")) {
    const auto number = std::stoi(std::string(key.substr(1)));
    if (number >= 1 && number <= 24) {
      function = static_cast<unichar>(NSF1FunctionKey + number - 1);
    }
  }
  return function == 0 ? @""
                       : [NSString stringWithCharacters:&function length:1];
}

static auto registry_native_modifiers(std::uint8_t modifiers)
    -> NSEventModifierFlags {
  auto output = NSEventModifierFlags{};
  if ((modifiers & static_cast<std::uint8_t>(liquify::KeyModifier::primary)) !=
      0) {
    output |= NSEventModifierFlagCommand;
  }
  if ((modifiers & static_cast<std::uint8_t>(liquify::KeyModifier::control)) !=
      0) {
    output |= NSEventModifierFlagControl;
  }
  if ((modifiers & static_cast<std::uint8_t>(liquify::KeyModifier::alt)) != 0) {
    output |= NSEventModifierFlagOption;
  }
  if ((modifiers & static_cast<std::uint8_t>(liquify::KeyModifier::shift)) !=
      0) {
    output |= NSEventModifierFlagShift;
  }
  return output;
}

static void add_registry_menu(NSMenu *main_menu, std::string_view title,
                              std::span<const RegistryMenuEntry> entries) {
  auto *native_title =
      [NSString stringWithUTF8String:std::string(title).c_str()];
  auto *root = [[NSMenuItem alloc] initWithTitle:native_title
                                          action:nil
                                   keyEquivalent:@""];
  auto *menu = [[NSMenu alloc] initWithTitle:native_title];
  for (const auto &entry : entries) {
    auto *item = [[NSMenuItem alloc]
        initWithTitle:[NSString stringWithUTF8String:entry.title.c_str()]
               action:@selector(performLiquifyCommand:)
        keyEquivalent:registry_key_equivalent(entry.key)];
    [item setKeyEquivalentModifierMask:registry_native_modifiers(
                                           entry.modifiers)];
    [item setTag:entry.command];
    [item setTarget:native_menu_target];
    [menu addItem:item];
  }
  [root setSubmenu:menu];
  [main_menu addItem:root];
}

static void
install_registry_native_menu(std::span<const RegistryMenuEntry> entries) {
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
  auto *quit = [[NSMenuItem alloc] initWithTitle:@"Quit liquify"
                                          action:@selector(terminate:)
                                   keyEquivalent:@"q"];
  [application_menu addItem:quit];
  [application_root setSubmenu:application_menu];
  [main_menu addItem:application_root];
  auto first = std::size_t{};
  while (first < entries.size()) {
    auto last = first + 1;
    while (last < entries.size() &&
           entries[last].group == entries[first].group) {
      ++last;
    }
    add_registry_menu(main_menu, entries[first].group,
                      entries.subspan(first, last - first));
    first = last;
  }
  [NSApp setMainMenu:main_menu];
}

#endif

namespace liquify {
namespace {

constexpr float editor_padding = design::space2;
constexpr float editor_gutter_width = 72.0F;
constexpr float activity_bar_width = design::control_lg;
constexpr float divider_size = design::space1;
constexpr float status_bar_height = design::space6;

auto key_name(std::uint32_t key) -> std::optional<std::string> {
  if (key >= SDLK_A && key <= SDLK_Z) {
    return std::string(1, static_cast<char>('A' + key - SDLK_A));
  }
  if (key >= SDLK_0 && key <= SDLK_9) {
    return std::string(1, static_cast<char>('0' + key - SDLK_0));
  }
  if (key >= SDLK_F1 && key <= SDLK_F24) {
    return "F" + std::to_string(key - SDLK_F1 + 1);
  }
  switch (key) {
  case SDLK_BACKSLASH:
    return "Backslash";
  case SDLK_BACKSPACE:
    return "Backspace";
  case SDLK_DELETE:
    return "Delete";
  case SDLK_DOWN:
    return "Down";
  case SDLK_END:
    return "End";
  case SDLK_ESCAPE:
    return "Escape";
  case SDLK_HOME:
    return "Home";
  case SDLK_LEFT:
    return "Left";
  case SDLK_PAGEDOWN:
    return "PageDown";
  case SDLK_PAGEUP:
    return "PageUp";
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    return "Enter";
  case SDLK_RIGHT:
    return "Right";
  case SDLK_SPACE:
    return "Space";
  case SDLK_TAB:
    return "Tab";
  case SDLK_UP:
    return "Up";
  default:
    return std::nullopt;
  }
}

auto key_chord(std::uint32_t key, std::uint16_t modifiers)
    -> std::optional<KeyChord> {
  auto name = key_name(key);
  if (!name) {
    return std::nullopt;
  }
  auto output = KeyChord{.key = std::move(*name)};
#if defined(__APPLE__)
  if ((modifiers & SDL_KMOD_GUI) != 0) {
    output.modifiers |= static_cast<std::uint8_t>(KeyModifier::primary);
  }
  if ((modifiers & SDL_KMOD_CTRL) != 0) {
    output.modifiers |= static_cast<std::uint8_t>(KeyModifier::control);
  }
#else
  if ((modifiers & SDL_KMOD_CTRL) != 0) {
    output.modifiers |= static_cast<std::uint8_t>(KeyModifier::primary);
  }
#endif
  if ((modifiers & SDL_KMOD_ALT) != 0) {
    output.modifiers |= static_cast<std::uint8_t>(KeyModifier::alt);
  }
  if ((modifiers & SDL_KMOD_SHIFT) != 0) {
    output.modifiers |= static_cast<std::uint8_t>(KeyModifier::shift);
  }
  return output;
}

auto imgui_color(design::Rgba color) -> ImVec4 {
  return {color.red, color.green, color.blue, color.alpha};
}

auto packed_color(design::Rgba color) -> ImU32 {
  return ImGui::ColorConvertFloat4ToU32(imgui_color(color));
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

auto deploy_outcome_name(liquid::sync_ref_outcome_kind outcome)
    -> std::string_view {
  switch (outcome) {
  case liquid::sync_ref_outcome_kind::applied:
    return "applied";
  case liquid::sync_ref_outcome_kind::refused:
    return "refused";
  }
  return "unknown";
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

  auto render(SDL_Renderer *renderer, EditorBuffer &buffer,
              TextLayoutCache &layouts, ImFont &font,
              const design::Palette &palette,
              std::span<const Diagnostic> diagnostics,
              std::span<const SyntaxSpan> syntax,
              std::span<const FoldRange> folds,
              std::span<const AttributionSnapshot> attribution,
              std::span<const PresenceSnapshot> presence,
              const std::optional<liquid::drive_path_status> &drive,
              std::uint64_t syntax_generation, float x, float y, float width,
              float height, std::size_t first_line, float line_height,
              float cell_width, float horizontal_scroll,
              std::span<const ReviewDiffLine> review_lines,
              std::span<const std::size_t> comment_lines) -> std::size_t {
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
  first_run_state_path_ = default_first_run_state_path();
  if (!options_.screenshot_state) {
    auto loaded = load_first_run_state(first_run_state_path_);
    first_run_ = std::move(loaded.state);
    if (!loaded.error.empty()) {
      editor_status_ = std::move(loaded.error);
    }
  }
  if (options_.repository_path) {
    auto loaded_state = load_app_shell_state(*options_.repository_path);
    shell_state_ = loaded_state.state;
    shell_state_.theme = settings_.theme;
    if (!loaded_state.error.empty()) {
      editor_status_ = std::move(loaded_state.error);
    }
    repository_ = std::make_unique<RepositoryView>(
        RepositoryView::open(*options_.repository_path));
    if (repository_->error().empty()) {
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
      assistant_ =
          std::make_unique<Assistant>(config, key.configured, [secret] {
            return std::make_unique<AnthropicProvider>(secret);
          });
      if (options_.lake_url) {
        try {
          const auto *token = std::getenv("LIQUID_TOKEN");
          review_ = std::make_unique<ReviewController>(
              *options_.repository_path, *options_.lake_url,
              token == nullptr ? std::string{} : std::string{token});
          review_->request_list();
        } catch (const std::exception &error) {
          editor_status_ = std::string{"review configuration: "} + error.what();
        }
      }
    }
    if (repository_->error().empty()) {
      first_run_.repository_opened(repository_summary());
      persist_first_run();
    } else {
      first_run_.open_failed(*options_.repository_path,
                             std::string(repository_->error()));
    }
  }
  if (!plugin_host_ && !options_.screenshot_state && !options_.smoke) {
    auto plugins = load_global_plugins();
    if (!plugins) {
      plugin_host_ = std::make_shared<PluginHost>();
      auto message = plugins.error().message;
      if (!plugins.error().context.empty()) {
        message += ": " + plugins.error().context;
      }
      plugin_failures_.push_back({{}, std::move(message)});
    } else {
      plugin_host_ = std::move(plugins->host);
      plugin_failures_.insert(
          plugin_failures_.end(),
          std::make_move_iterator(plugins->failures.begin()),
          std::make_move_iterator(plugins->failures.end()));
    }
  }
  panels_.restore(shell_state_.panel_layout);
  if (options_.smoke) {
    editor_ = std::make_unique<EditorBuffer>(EditorBuffer::from_text(
        "smoke-50000", "smoke-50000.txt", smoke_text()));
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
      first_run_.clone_failed("lake.orama.com is unreachable", true);
    }
  }
  if (screenshot != nullptr && screenshot->view != ScreenshotView::welcome &&
      screenshot->view != ScreenshotView::repository_unreadable &&
      screenshot->view != ScreenshotView::lake_offline) {
    editor_ = std::make_unique<EditorBuffer>(EditorBuffer::from_text(
        "screenshot-main", "src/app.cpp", screenshot_text()));
    layouts_ = std::make_unique<TextLayoutCache>();
    layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
    text_layer_ = std::make_unique<RetainedTextLayer>();
    text_layer_->reset(editor_->line_count());
    editor_focused_ = true;
    if (screenshot->view == ScreenshotView::review) {
      panels_.focus(PanelId::review);
    } else if (screenshot->view == ScreenshotView::editor_view) {
      panels_.focus(PanelId::search);
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
    if (screenshot->view == ScreenshotView::settings) {
      settings_open_ = true;
      settings_.theme = screenshot->theme == ScreenshotTheme::dark
                            ? ThemePreference::dark
                            : ThemePreference::light;
      settings_.editor.wrap = true;
      settings_.keymap["run.build"] = "F7";
      settings_.lake_accounts.push_back(
          {"orama", "https://api.oramasearch.com/liquid"});
    }
  }
  configure_commands();
  if (screenshot != nullptr &&
      screenshot->view == ScreenshotView::command_palette) {
    commands_->record_use("run.build");
    commands_->record_use("file.save");
    command_palette_open_ = true;
  }
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

void App::activate_repository(std::unique_ptr<RepositoryView> repository) {
  if (repository_) {
    persist_shell_state();
  }
  editor_.reset();
  layouts_.reset();
  text_layer_.reset();
  editor_graph_.reset();
  review_.reset();
  assistant_.reset();
  plugin_host_.reset();
  plugin_failures_.clear();
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
  if (options_.lake_url) {
    try {
      const auto *token = std::getenv("LIQUID_TOKEN");
      review_ = std::make_unique<ReviewController>(
          *options_.repository_path, *options_.lake_url,
          token == nullptr ? std::string{} : std::string{token});
      review_->request_list();
    } catch (const std::exception &error) {
      editor_status_ = std::string{"review configuration: "} + error.what();
    }
  }
  configure_commands();
  update_window_title();
}

void App::begin_open_repository(std::filesystem::path path) {
  if (repository_operation_) {
    return;
  }
  first_run_.begin_open(path);
  repository_operation_is_clone_ = false;
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
  options_.lake_url = remote;
  repository_operation_is_clone_ = true;
  const auto *configured_token = std::getenv("LIQUID_TOKEN");
  const auto token = configured_token == nullptr
                         ? std::string{}
                         : std::string{configured_token};
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
        first_run_.clone_failed(std::string{repository->error()},
                                repository->offline());
      } else {
        first_run_.open_failed(first_run_.pending_path(),
                               std::string{repository->error()});
      }
    } else {
      activate_repository(std::move(repository));
      first_run_.repository_opened(repository_summary());
      persist_first_run();
    }
  } catch (const std::exception &error) {
    if (repository_operation_is_clone_) {
      first_run_.clone_failed(error.what(), false);
    } else {
      first_run_.open_failed(first_run_.pending_path(), error.what());
    }
  }
  repository_operation_.reset();
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
  const char *arguments[] = {executable.c_str(),
                             repository ? repository_argument.c_str() : nullptr,
                             nullptr};
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

App::~App() { shutdown(); }

void App::configure_commands() {
  commands_ = std::make_unique<CommandRegistry>();
  for (const auto &command : builtin_commands()) {
    auto added = commands_->add(command);
    if (!added) {
      command_diagnostics_.push_back(added.error());
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
  for (const auto &panel : standard_panel_descriptors()) {
    for (const auto &panel_command : panel.commands) {
      if (commands_->find(panel_command.id) != nullptr) {
        continue;
      }
      auto added = commands_->add(
          {std::string(panel_command.id),
           std::string(panel_command.title),
           std::string(panel.title),
           "Command provided by the " + std::string(panel.title) + " panel.",
           {}});
      if (!added) {
        command_diagnostics_.push_back(added.error());
      }
    }
  }
  if (screenshot_mode() || options_.smoke) {
    return;
  }
  const auto apply_file = [this](const std::filesystem::path &path) {
    auto loaded = load_keymap(path);
    for (const auto &diagnostic : loaded.diagnostics) {
      command_diagnostics_.push_back(format_command_diagnostic(diagnostic));
    }
    auto diagnostics = commands_->apply_overrides(loaded.overrides);
    for (const auto &diagnostic : diagnostics) {
      command_diagnostics_.push_back(format_command_diagnostic(diagnostic));
    }
  };
  apply_file(global_settings_path());
  if (options_.repository_path) {
    apply_file(repository_settings_path(*options_.repository_path));
  }
  auto settings_overrides = std::vector<KeymapOverride>{};
  settings_overrides.reserve(settings_.keymap.size());
  for (const auto &[name, binding] : settings_.keymap) {
    auto command_name = name;
    if (name == "build.run") {
      command_name = "run.build";
    } else if (name == "tests.run") {
      command_name = "run.tests";
    } else if (name == "deploy.run") {
      command_name = "run.deploy";
    } else if (name == "palette.open") {
      command_name = "edit.command-palette";
    }
    settings_overrides.push_back(
        {std::move(command_name), {binding}, {user_settings_path(), 1, 1}});
  }
  auto diagnostics = commands_->apply_overrides(settings_overrides);
  for (const auto &diagnostic : diagnostics) {
    command_diagnostics_.push_back(format_command_diagnostic(diagnostic));
  }
  if (!command_diagnostics_.empty()) {
    editor_status_ = command_diagnostics_.front();
  } else if (!commands_->conflicts().empty()) {
    editor_status_ = "keymap conflict " +
                     commands_->conflicts().front().binding +
                     ": open the command palette";
  }
}

bool App::invoke_shell_command(std::string_view name) {
  if (name == "file.save") {
    if (review_diff_open_) {
      editor_status_ = "review diffs are read-only";
    } else {
      save_editor();
    }
    return true;
  }
  if (name == "edit.command-palette") {
    command_palette_open_ = true;
    return true;
  }
  if (name == "settings.open") {
    settings_open_ = true;
    return true;
  }
  if (name == "view.toggle-sidebar") {
    shell_state_.sidebar_visible = !shell_state_.sidebar_visible;
    persist_shell_state();
    return true;
  }
  if (name == "view.toggle-bottom-panel") {
    shell_state_.bottom_panel_visible = !shell_state_.bottom_panel_visible;
    persist_shell_state();
    return true;
  }
  if (name == "view.toggle-editor-split") {
    shell_state_.editor_split = !shell_state_.editor_split;
    persist_shell_state();
    return true;
  }
  if (name == "view.cycle-theme") {
    cycle_theme();
    return true;
  }
  if (name == "panel.reset-layout") {
    reset_panel_layout();
    return true;
  }
  const auto show_panel = [this, name](std::string_view command, PanelId id) {
    if (name != command) {
      return false;
    }
    panels_.focus(id);
    const auto placement = panels_.layout(id).placement;
    if (placement == PanelPlacement::sidebar) {
      shell_state_.sidebar_visible = true;
    } else if (placement == PanelPlacement::bottom) {
      shell_state_.bottom_panel_visible = true;
    }
    persist_shell_state();
    return true;
  };
  return show_panel("view.repository", PanelId::repository) ||
         show_panel("view.search", PanelId::search) ||
         show_panel("view.review", PanelId::review) ||
         show_panel("view.build", PanelId::build) ||
         show_panel("view.multiplayer", PanelId::multiplayer) ||
         show_panel("panel.diagnostics", PanelId::diagnostics) ||
         show_panel("panel.build", PanelId::build) ||
         show_panel("panel.assistant", PanelId::assistant) ||
         show_panel("panel.plugins", PanelId::plugins);
}

bool App::invoke_editor_command(std::string_view name) {
  if (name == "edit.select-all") {
    if (editor_) {
      editor_->select_all();
    }
    return true;
  }
  if (name == "edit.toggle-input-mode") {
    if (editor_) {
      const auto next = editor_->mode() == EditingMode::standard
                            ? EditingMode::modal_command
                            : EditingMode::standard;
      editor_->set_mode(next);
    }
    return true;
  }
  if (name == "edit.find") {
    if (editor_) {
      find_open_ = true;
      buffer_matches_ = editor_->find(find_query_, find_options_);
    }
  } else if (name == "edit.focus-search") {
    panels_.focus(PanelId::search);
    panels_.clear_focus_request();
    shell_state_.sidebar_visible = true;
    persist_shell_state();
  } else if (name == "edit.undo") {
    if (editor_graph_) {
      editor_graph_->request_undo();
    }
  } else if (name == "edit.redo") {
    if (editor_graph_) {
      editor_graph_->request_redo();
    }
  } else if (name == "selection.line") {
    if (editor_) {
      editor_->select(SelectionKind::line);
    }
  } else if (name == "selection.add-cursor-up" ||
             name == "selection.add-cursor-down") {
    if (editor_) {
      const auto cursor = editor_->cursor();
      if (name == "selection.add-cursor-up" && cursor.line > 0) {
        editor_->add_cursor({cursor.line - 1, cursor.byte});
      } else if (name == "selection.add-cursor-down" &&
                 cursor.line + 1 < editor_->line_count()) {
        editor_->add_cursor({cursor.line + 1, cursor.byte});
      }
    }
  } else if (name == "selection.expand") {
    expand_structural_selection();
  } else if (name == "selection.shrink") {
    shrink_structural_selection();
  } else if (name == "selection.enclosing-definition") {
    select_enclosing_definition();
  } else if (name == "selection.symbol-references") {
    select_symbol_references();
  } else if (name == "go.definition") {
    request_definition();
  } else if (name == "go.hover") {
    request_hover();
  } else if (name == "go.completion") {
    request_completion();
  } else if (name == "refactor.rename") {
    open_rename();
  } else if (name == "refactor.extract-function") {
    open_extract_function();
  } else {
    return false;
  }
  return true;
}

bool App::invoke_run_command(std::string_view name) {
  if (name == "run.build") {
    request_build();
  } else if (name == "run.tests") {
    request_tests();
  } else if (name == "run.deploy") {
    request_deploy();
  } else if (name == "review.refresh") {
    execute_panel_command(name);
  } else if (name == "review.next-hunk") {
    navigate_hunk(true);
  } else if (name == "review.previous-hunk") {
    navigate_hunk(false);
  } else if (name == "review.next-comment") {
    navigate_comment();
  } else if (name == "review.new-comment") {
    begin_review_comment();
  } else if (name == "review.approve") {
    if (review_) {
      review_->request_approve();
    }
  } else {
    return false;
  }
  return true;
}

bool App::invoke_assistant_command(std::string_view name) {
  if (name == "assistant.ask") {
    request_assistant_context(AssistantAction::ask);
  } else if (name == "assistant.explain-diagnostic") {
    request_assistant_context(AssistantAction::explain_diagnostic);
  } else if (name == "assistant.propose-edit") {
    request_assistant_context(AssistantAction::propose_edit);
  } else if (name == "assistant.accept-edit") {
    accept_assistant_edit();
  } else if (name == "assistant.reject-edit") {
    if (assistant_) {
      assistant_->reject();
    }
  } else if (name == "assistant.toggle") {
    toggle_assistant();
  } else if (name == "assistant.preview-context") {
    request_assistant_context(std::nullopt);
  } else {
    return false;
  }
  return true;
}

bool App::invoke_command(std::string_view name) {
  const auto *command = commands_ ? commands_->find(name) : nullptr;
  if (command == nullptr) {
    return false;
  }
  commands_->record_use(name);
  if (command->plugin) {
    invoke_plugin_command(name);
    return true;
  }
  if (invoke_shell_command(name) || invoke_editor_command(name) ||
      invoke_run_command(name) || invoke_assistant_command(name)) {
    return true;
  }
  if (name == "help.version") {
    editor_status_ = "liquify " + std::string(version_string()) +
                     " (libliquid " + std::string(liquid::version_string()) +
                     ")";
    return true;
  }
  if (name == "help.command-reference") {
    editor_status_ =
        "run liquify docs commands to generate the command reference";
    return true;
  }
  for (const auto &panel : standard_panel_descriptors()) {
    const auto found =
        std::ranges::find(panel.commands, name, &PanelCommand::id);
    if (found != panel.commands.end()) {
      execute_panel_command(name);
      return true;
    }
  }
  return false;
}

bool App::init() {
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
          ? SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
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
                       ? std::max(SDL_GetWindowDisplayScale(window_), 1.0F)
                       : 1.0F;
  if (screenshot != nullptr) {
    io.DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
    SDL_SetRenderScale(renderer_, 1.0F, 1.0F);
  }
  load_font_atlas(display_scale_);
  apply_theme();
  ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
  ImGui_ImplSDLRenderer3_Init(renderer_);
  SDL_StartTextInput(window_);
#if defined(__APPLE__) && defined(__OBJC__)
  if (!screenshot_mode()) {
    auto entries = std::vector<RegistryMenuEntry>{};
    native_menu_commands_.clear();
    for (const auto &command : commands_->commands()) {
      native_menu_commands_.push_back(command.name);
      auto entry = RegistryMenuEntry{
          command.group,
          command.title,
          {},
          0,
          static_cast<std::int32_t>(native_menu_commands_.size())};
      const auto bindings = commands_->bindings(command.name);
      if (!bindings.empty() &&
          commands_->command_for(bindings.front()) != nullptr) {
        entry.key = bindings.front().key;
        entry.modifiers = bindings.front().modifiers;
      }
      entries.push_back(std::move(entry));
    }
    install_registry_native_menu(entries);
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
  auto ui_distance = std::numeric_limits<float>::max();
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
  }
  auto editor_distance = std::numeric_limits<float>::max();
  for (const auto size : {design::text12, design::text14, design::text16}) {
    add_font(design::ibm_plex_mono_regular, size, "IBM Plex Mono");
    const auto distance =
        std::abs(size - static_cast<float>(settings_.fonts.editor_size));
    if (distance < editor_distance) {
      editor_distance = distance;
      editor_font_index_ = io.Fonts->Fonts.Size - 1;
    }
  }
}

void App::apply_theme() {
  const auto dark = dark_theme_active();
  const auto &palette = dark ? design::dark_palette : design::light_palette;
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
  style.WindowBorderSize = design::border_width;
  style.ChildBorderSize = design::border_width;
  style.PopupBorderSize = design::border_width;
  style.FrameBorderSize = 0.0F;
  style.TabBorderSize = 0.0F;

  auto *colors = style.Colors;
  colors[ImGuiCol_Text] = imgui_color(palette.text_primary);
  colors[ImGuiCol_TextDisabled] = imgui_color(palette.text_disabled);
  colors[ImGuiCol_WindowBg] = imgui_color(palette.canvas);
  colors[ImGuiCol_ChildBg] = imgui_color(palette.surface);
  colors[ImGuiCol_PopupBg] = imgui_color(palette.overlay);
  colors[ImGuiCol_Border] = imgui_color(palette.border_subtle);
  colors[ImGuiCol_BorderShadow] = {0.0F, 0.0F, 0.0F, 0.0F};
  colors[ImGuiCol_FrameBg] = imgui_color(palette.layer);
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
  colors[ImGuiCol_Button] = imgui_color(palette.layer);
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
  if (options_.repository_path) {
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

void App::open_editor(std::string path, std::string blob_id) {
  if (analysis_buffer_open_ && editor_graph_ && editor_) {
    editor_graph_->close_buffer(std::filesystem::path(editor_->display_path()));
  }
  analysis_buffer_open_ = false;
  analysis_text_.clear();
  review_diff_open_ = false;
  structural_selection_history_.clear();
  structural_references_.clear();
  editor_ = std::make_unique<EditorBuffer>(EditorBuffer::open(
      *options_.repository_path, std::move(path), std::move(blob_id)));
  layouts_ = std::make_unique<TextLayoutCache>();
  layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
  text_layer_ = std::make_unique<RetainedTextLayer>();
  text_layer_->reset(editor_->line_count());
  scroll_line_ = 0;
  horizontal_scroll_ = 0.0F;
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

void App::show_review_diff(const ReviewSnapshot &review) {
  if (!review.selected || !review.head) {
    return;
  }
  if (analysis_buffer_open_ && editor_graph_ && editor_) {
    editor_graph_->close_buffer(std::filesystem::path(editor_->display_path()));
  }
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
  editor_ = std::make_unique<EditorBuffer>(EditorBuffer::from_text(
      "review:" + review.selected->id, title, std::move(text)));
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
  if (!editor_) {
    return;
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
      return;
    }
    saved = editor_->save(*repository);
  } else {
    saved = editor_->save();
  }
  if (!saved) {
    editor_status_ = std::string(editor_->error());
    return;
  }
  editor_status_ = "saved as a drip";
  if (editor_graph_) {
    editor_graph_->note_saved_drip(editor_->pending_drips());
    editor_graph_->refresh_status();
    if (build_running) {
      request_build();
      editor_status_ = "saved as a drip; build replanned";
    }
  }
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

void App::request_deploy() {
  if (editor_graph_) {
    editor_graph_->request_deploy();
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
      open_editor(path, "history:" + path);
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
  settings_.ai.enabled = enable;
  apply_settings_live();
  persist_settings();
  editor_status_ = enable ? "AI on" : "AI off";
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
      const auto current_path =
          editor_ ? std::string(editor_->display_path()) : std::string{};
      const auto changed_current = std::ranges::any_of(
          snapshot.structural_result->applied,
          [&current_path](const auto &item) {
            return item.path.generic_string() == current_path;
          });
      structural_preview_open_ = false;
      structural_preview_.clear();
      if (changed_current) {
        open_editor(current_path, "structural:" + current_path);
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
    if (snapshot.definition) {
      const auto target = snapshot.definition->path.generic_string();
      if (!editor_ || editor_->display_path() != target) {
        open_editor(target, "graph:" + target);
      }
      const auto &range = snapshot.definition->declaration.range;
      editor_->set_cursor({range.start_line, range.start_column});
      ensure_cursor_visible();
    }
  }
}

void App::handle_text(std::string_view text) {
  if (command_palette_visible_ || !editor_ || !editor_focused_ ||
      review_diff_open_ || editor_->mode() == EditingMode::modal_command) {
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

void App::handle_key(std::uint32_t key, std::uint16_t modifiers) {
  if (command_palette_visible_) {
    return;
  }
  const auto chord = key_chord(key, modifiers);
  const auto *command =
      chord && commands_ ? commands_->command_for(*chord) : nullptr;
  if (command != nullptr) {
    invoke_command(command->name);
    return;
  }
  const auto primary = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
  const auto shift = (modifiers & SDL_KMOD_SHIFT) != 0;
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
  if (key == SDLK_F7) {
    request_build();
    return;
  }
  if (key == SDLK_F8) {
    request_tests();
    return;
  }
  if (key == SDLK_F9) {
    request_deploy();
    return;
  }
  if (!editor_) {
    return;
  }
  if (!editor_focused_) {
    return;
  }
  const auto alt = (modifiers & SDL_KMOD_ALT) != 0;
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
    if (editor_graph_) {
      if (shift) {
        editor_graph_->request_redo();
      } else {
        editor_graph_->request_undo();
      }
    }
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
  if (primary && key == SDLK_S) {
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

void App::draw_repository_tree() {
  const auto path = options_.repository_path->string();
  ImGui::Text("Repository: %s", path.c_str());
  ImGui::TextDisabled("opened in %.3f ms", repository_->open_time().count());
  ImGui::Separator();
  if (!repository_->error().empty()) {
    ImGui::TextWrapped("Could not open this repository: %s",
                       std::string(repository_->error()).c_str());
    return;
  }
  repository_->poll();
  if (repository_->entries().empty()) {
    ImGui::TextWrapped("This repository has no files.");
    return;
  }
  draw_repository_entries({});
}

void App::draw_review_panel() {
  ImGui::TextUnformatted("Reviews");
  if (!options_.lake_url) {
    ImGui::TextDisabled("Configure with --lake <repository-url>");
    return;
  }
  if (!review_) {
    ImGui::TextWrapped("%s", editor_status_.c_str());
    return;
  }
  const auto snapshot = review_->snapshot();
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
    ImGui::TextWrapped("Could not load reviews: %s", snapshot.error.c_str());
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
      ImGui::TextWrapped(
          "%s", panels_.descriptor(PanelId::review).empty_state.data());
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
  ImGui::TextDisabled("text diff %.3f ms | anchor map %.3f ms",
                      snapshot.review.open_to_first_hunk_ms,
                      snapshot.review.anchor_mapping_ms);
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
    ImGui::OpenPopup("Review comment");
    review_comment_popup_open_ = false;
  }
  if (!ImGui::BeginPopupModal("Review comment", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
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
    ImGui::PushID(path.c_str());
    if (entry.kind == liquid::node_kind::blob) {
      if (ImGui::Selectable(entry.name.c_str(),
                            editor_ && editor_->display_path() == path)) {
        open_editor(path, liquid::to_string(entry.id));
      }
    } else if (entry.kind == liquid::node_kind::tree) {
      const auto open =
          ImGui::TreeNodeEx(entry.name.c_str(), ImGuiTreeNodeFlags_OpenOnArrow);
      if (open) {
        if (!repository_->directory_loaded(path) &&
            !repository_->directory_loading(path)) {
          repository_->request_directory(path, entry.ref);
        }
        if (repository_->directory_loading(path)) {
          ImGui::TextDisabled("loading...");
        } else if (!repository_->directory_error(path).empty()) {
          ImGui::TextWrapped(
              "Unable to read directory: %s",
              std::string(repository_->directory_error(path)).c_str());
        } else {
          draw_repository_entries(path);
        }
        ImGui::TreePop();
      }
    } else {
      ImGui::Text("%s (%s)", entry.name.c_str(),
                  std::string(node_kind_name(entry.kind)).c_str());
    }
    ImGui::PopID();
  }
}

void App::draw_assistant() {
  if (!assistant_) {
    return;
  }
  const auto snapshot = assistant_->snapshot();
  ImGui::Text("Assistant: %s", snapshot.status.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("What will be sent")) {
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
    ImGui::OpenPopup("What will be sent");
    context_preview_open_ = false;
  }
  if (ImGui::BeginPopupModal("What will be sent", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
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
  } else if (command == "build.deploy") {
    request_deploy();
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

void App::draw_settings() {
  if (settings_open_) {
    ImGui::OpenPopup("Settings");
    settings_open_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(900.0F, 650.0F), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Settings", nullptr,
                              ImGuiWindowFlags_NoSavedSettings)) {
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
  if (ImGui::Checkbox("Repository overrides", &repository_scope)) {
    settings_scope_ =
        repository_scope ? SettingsScope::repository : SettingsScope::user;
  }
  if (!options_.repository_path) {
    ImGui::EndDisabled();
  }
  const auto path =
      settings_scope_ == SettingsScope::repository && options_.repository_path
          ? repository_settings_path(*options_.repository_path)
          : user_settings_path();
  ImGui::TextDisabled("Editing %s", path.string().c_str());
  for (const auto &diagnostic : settings_diagnostics_) {
    ImGui::TextColored(imgui_color((applied_dark_theme_ ? design::dark_palette
                                                        : design::light_palette)
                                       .warning),
                       "%s", diagnostic.message.c_str());
  }
  ImGui::Separator();
  ImGui::BeginChild("settings-scroll", ImVec2(0.0F, -36.0F),
                    ImGuiChildFlags_Borders);
  const auto begins = [this](SettingsGroup group) {
    return settings_group_matches(group, settings_query_) &&
           ImGui::CollapsingHeader(settings_group_name(group).data(),
                                   ImGuiTreeNodeFlags_DefaultOpen);
  };
  if (begins(SettingsGroup::appearance)) {
    if (ImGui::SmallButton("Reset appearance")) {
      reset_settings(SettingsGroup::appearance);
    }
    auto theme = static_cast<int>(settings_.theme);
    constexpr auto themes =
        std::array{"Follow operating system", "Light", "Dark"};
    if (ImGui::Combo("Theme", &theme, themes.data(),
                     static_cast<int>(themes.size()))) {
      settings_.theme = static_cast<ThemePreference>(theme);
      apply_settings_live();
      persist_settings();
    }
    auto ui_size = static_cast<int>(settings_.fonts.ui_size);
    if (ImGui::SliderInt("UI size", &ui_size, 11, 32)) {
      settings_.fonts.ui_size = static_cast<std::uint32_t>(ui_size);
      apply_settings_live(true);
      persist_settings();
    }
    auto editor_size = static_cast<int>(settings_.fonts.editor_size);
    if (ImGui::SliderInt("Editor size", &editor_size, 12, 16)) {
      settings_.fonts.editor_size = static_cast<std::uint32_t>(editor_size);
      apply_settings_live(true);
      persist_settings();
    }
  }
  if (begins(SettingsGroup::editor)) {
    if (ImGui::SmallButton("Reset editor")) {
      reset_settings(SettingsGroup::editor);
    }
    auto changed = ImGui::Checkbox("Wrap long lines", &settings_.editor.wrap);
    changed |= ImGui::Checkbox("Show whitespace", &settings_.editor.whitespace);
    changed |= ImGui::Checkbox("Format .liquid files on save",
                               &settings_.editor.format_on_save);
    if (changed) {
      apply_settings_live();
      persist_settings();
    }
  }
  if (begins(SettingsGroup::keymap)) {
    if (ImGui::SmallButton("Reset keymap")) {
      reset_settings(SettingsGroup::keymap);
    }
    ImGui::TextDisabled("Command registry bindings");
    for (const auto name : {"settings.open", "run.build", "run.tests",
                            "run.deploy", "edit.command-palette"}) {
      auto &binding = settings_.keymap[name];
      if (binding.empty() && commands_) {
        binding = commands_->binding_label(name);
      }
      ImGui::PushID(name);
      ImGui::SetNextItemWidth(180.0F);
      if (ImGui::InputText(name, &binding)) {
        auto override = KeymapOverride{name, {binding}, {path, 1, 1}};
        auto diagnostics = commands_->apply_overrides({&override, 1});
        command_diagnostics_.clear();
        for (const auto &diagnostic : diagnostics) {
          command_diagnostics_.push_back(format_command_diagnostic(diagnostic));
        }
        persist_settings();
      }
      ImGui::PopID();
    }
  }
  if (begins(SettingsGroup::ai)) {
    if (ImGui::SmallButton("Reset AI")) {
      reset_settings(SettingsGroup::ai);
    }
    if (ImGui::Checkbox("Enable AI surface", &settings_.ai.enabled)) {
      apply_settings_live();
      persist_settings();
    }
    if (ImGui::InputText("Model", &settings_.ai.model)) {
      apply_settings_live();
      persist_settings();
    }
    ImGui::TextDisabled("Credentials remain in the OS credential store.");
  }
  if (begins(SettingsGroup::lake)) {
    if (ImGui::SmallButton("Reset lake")) {
      reset_settings(SettingsGroup::lake);
    }
    for (const auto &account : settings_.lake_accounts) {
      ImGui::BulletText("%s — %s", account.name.c_str(), account.url.c_str());
    }
    ImGui::TextDisabled("Lake credentials remain in the OS credential store.");
  }
  if (begins(SettingsGroup::telemetry)) {
    if (ImGui::SmallButton("Reset telemetry")) {
      reset_settings(SettingsGroup::telemetry);
    }
    if (ImGui::Checkbox("Enable telemetry", &settings_.telemetry)) {
      persist_settings();
    }
    ImGui::TextDisabled("Off by default.");
  }
  ImGui::EndChild();
  if (ImGui::Button("Close")) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::draw_command_palette() {
  command_palette_visible_ = false;
  if (command_palette_open_) {
    ImGui::OpenPopup("Command palette");
    command_palette_open_ = false;
  }
  ImGui::SetNextWindowSize(ImVec2(760.0F, 520.0F), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Command palette", nullptr,
                              ImGuiWindowFlags_NoSavedSettings)) {
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
  auto recent = std::vector<std::string>{};
  if (command_palette_query_.empty()) {
    recent.assign(commands_->recent().begin(), commands_->recent().end());
  }
  auto matches = commands_->search(command_palette_query_);
  if (command_palette_query_.empty()) {
    matches.erase(std::remove_if(matches.begin(), matches.end(),
                                 [&](const auto &match) {
                                   return std::ranges::find(recent,
                                                            match.name) !=
                                          recent.end();
                                 }),
                  matches.end());
  }
  const auto result_count = recent.size() + matches.size();
  auto selection_moved = false;
  if (result_count > 0 && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    command_palette_selection_ =
        (command_palette_selection_ + 1) % result_count;
    selection_moved = true;
  }
  if (result_count > 0 && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    command_palette_selection_ = command_palette_selection_ == 0
                                     ? result_count - 1
                                     : command_palette_selection_ - 1;
    selection_moved = true;
  }
  if (command_palette_selection_ >= result_count) {
    command_palette_selection_ = 0;
  }
  auto pending_command = std::optional<std::string>{};
  if (result_count > 0 && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    if (command_palette_selection_ < recent.size()) {
      pending_command = recent[command_palette_selection_];
    } else {
      pending_command =
          matches[command_palette_selection_ - recent.size()].name;
    }
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  const auto &palette =
      applied_dark_theme_ ? design::dark_palette : design::light_palette;
  for (const auto &diagnostic : command_diagnostics_) {
    ImGui::TextColored(imgui_color(palette.warning), "%s", diagnostic.c_str());
  }
  for (const auto &conflict : commands_->conflicts()) {
    auto message = "Key conflict " + conflict.binding + ": ";
    for (std::size_t index = 0; index < conflict.commands.size(); ++index) {
      if (index != 0) {
        message += ", ";
      }
      message += conflict.commands[index];
    }
    ImGui::TextColored(imgui_color(palette.warning), "%s", message.c_str());
  }
  ImGui::Separator();
  auto row_index = std::size_t{};
  const auto draw_command = [this, &pending_command, &row_index,
                             selection_moved](std::string_view name) {
    const auto *definition = commands_->find(name);
    if (definition == nullptr) {
      return;
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const auto label = definition->title + "##" + definition->name;
    const auto selected = row_index == command_palette_selection_;
    if (ImGui::Selectable(label.c_str(), selected,
                          ImGuiSelectableFlags_SpanAllColumns)) {
      pending_command = std::string(name);
    }
    if (selected && selection_moved) {
      ImGui::SetScrollHereY();
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", definition->group.c_str());
    ImGui::TableSetColumnIndex(2);
    const auto binding = commands_->binding_label(name);
    ImGui::TextDisabled("%s", binding.c_str());
    ++row_index;
  };
  if (ImGui::BeginTable("command-results", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0.0F, -1.0F))) {
    ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch,
                            0.62F);
    ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthStretch, 0.18F);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.20F);
    ImGui::TableHeadersRow();
    if (!recent.empty()) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextDisabled("Recently used");
      for (const auto &name : recent) {
        draw_command(name);
      }
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::SeparatorText("All commands");
    }
    for (const auto &match : matches) {
      draw_command(match.name);
    }
    ImGui::EndTable();
  }
  if (pending_command) {
    invoke_command(*pending_command);
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
    ImGui::OpenPopup("Rename symbol");
    rename_open_ = false;
  }
  if (ImGui::BeginPopupModal("Rename symbol", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("New name", &structural_name_);
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
    ImGui::OpenPopup("Extract function");
    extract_function_open_ = false;
  }
  if (ImGui::BeginPopupModal("Extract function", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("Function name", &structural_name_);
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
    if (ImGui::Checkbox(label.c_str(), &accepted)) {
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
      {.message = std::to_string(plugin_panels_.size()) + " plugin panels",
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
  if (plugin_failures_.empty() && plugin_panels_.empty()) {
    return;
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Plugins");
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

void App::draw_editor() {
  const auto path = std::string(editor_->display_path());
  const auto graph_snapshot =
      editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
  ImGui::Text("%s%s", path.c_str(), editor_->dirty() ? " *" : "");
  ImGui::SameLine();
  if (review_diff_open_) {
    ImGui::TextDisabled("read-only text diff");
  } else if (ImGui::Button(editing_mode_name(editor_->mode()).data())) {
    const auto next = editor_->mode() == EditingMode::standard
                          ? EditingMode::modal_command
                          : EditingMode::standard;
    editor_->set_mode(next);
    editor_focused_ = true;
  }
  if (!review_diff_open_) {
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
      save_editor();
      editor_focused_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Build (F7)")) {
      request_build();
    }
    ImGui::SameLine();
    if (ImGui::Button("Tests (F8)")) {
      request_tests();
    }
    ImGui::SameLine();
    if (ImGui::Button("Deploy (F9)")) {
      request_deploy();
    }
  }
  if (assistant_) {
    ImGui::SameLine();
    const auto enabled = assistant_->snapshot().enabled;
    if (ImGui::Button(enabled ? "AI: on" : "AI: off")) {
      toggle_assistant();
    }
  }
  if (analysis_buffer_open_) {
    ImGui::SameLine();
    if (ImGui::Button("Definition (F12)")) {
      request_definition();
    }
    ImGui::SameLine();
    if (ImGui::Button("Hover (F1)")) {
      request_hover();
    }
    ImGui::SameLine();
    if (ImGui::Button("Complete")) {
      request_completion();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(shell_state_.editor_split ? "Unsplit" : "Split")) {
    shell_state_.editor_split = !shell_state_.editor_split;
    persist_shell_state();
  }
  ImGui::SameLine();
  if (ImGui::Button(shell_state_.bottom_panel_visible ? "Hide panel"
                                                      : "Show panel")) {
    shell_state_.bottom_panel_visible = !shell_state_.bottom_panel_visible;
    persist_shell_state();
  }
  if (!editor_status_.empty()) {
    ImGui::SameLine();
    ImGui::TextUnformatted(editor_status_.c_str());
  } else if (!editor_->error().empty()) {
    ImGui::SameLine();
    const auto error = std::string(editor_->error());
    ImGui::TextUnformatted(error.c_str());
  }
  if (!editor_mutation_allowed()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.75F, 0.20F, 0.15F, 1.0F),
                       "edit refused: another session holds a lease");
  }
  if (!review_diff_open_ && options_.repository_path) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Take lease")) {
      take_editor_lease();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Release lease")) {
      release_editor_lease();
    }
  }
  if (multiplayer_) {
    ImGui::Separator();
    ImGui::TextUnformatted("Presence");
    auto presence_count = std::size_t{};
    for (const auto &value : multiplayer_->presence) {
      if (value.session == multiplayer_session_) {
        continue;
      }
      ImGui::TextUnformatted(multiplayer_presence_text(value).c_str());
      ++presence_count;
    }
    if (presence_count == 0) {
      ImGui::TextDisabled("No other active sessions");
    }
    ImGui::TextUnformatted("Attribution gutter");
    const auto cursor_line = editor_->cursor().line + 1;
    const auto first = cursor_line > 2 ? cursor_line - 2 : 1;
    const auto last = cursor_line + 2;
    for (const auto &value : multiplayer_->attribution) {
      if (value.line >= first && value.line <= last) {
        ImGui::TextUnformatted(multiplayer_attribution_text(value).c_str());
      }
    }
    for (const auto &value : multiplayer_->leases) {
      ImGui::TextUnformatted(multiplayer_lease_text(value).c_str());
    }
  }
  if (editor_graph_) {
    const auto status = editor_graph_status(graph_snapshot);
    ImGui::TextWrapped("%s", status.c_str());
    if (!graph_snapshot.query_error.empty()) {
      ImGui::TextColored(ImVec4(0.75F, 0.12F, 0.12F, 1.0F), "%s",
                         graph_snapshot.query_error.c_str());
    }
    if (graph_snapshot.hover) {
      ImGui::TextWrapped("%s", graph_snapshot.hover->signature.c_str());
      if (!graph_snapshot.hover->documentation.empty()) {
        ImGui::TextWrapped("%s", graph_snapshot.hover->documentation.c_str());
      }
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
    if (ImGui::Checkbox("Regex", &find_options_.regular_expression)) {
      buffer_matches_ = editor_->find(find_query_, find_options_);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Case", &find_options_.case_sensitive)) {
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
  const auto &palette =
      applied_dark_theme_ ? design::dark_palette : design::light_palette;
  draw_list->AddRectFilled(position,
                           ImVec2(position.x + size.x, position.y + size.y),
                           packed_color(palette.surface));
  draw_list->AddRect(position, ImVec2(position.x + size.x, position.y + size.y),
                     packed_color(palette.border_subtle));

  editor_x_ = position.x;
  editor_y_ = position.y;
  editor_width_ = size.x;
  editor_height_ = size.y;
  line_height_ = ImGui::GetFontSize() + 4.0F;
  const auto *glyph = ImGui::GetFont()->FindGlyph('M');
  cell_width_ =
      glyph == nullptr ? ImGui::GetFontSize() * 0.6F : glyph->AdvanceX;
  visible_lines_ =
      std::max<std::size_t>(1, static_cast<std::size_t>(size.y / line_height_));
  editor_visible_ = true;

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
        if (diagnostic != graph_snapshot.diagnostics.end()) {
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
      const auto attribution = std::ranges::find(
          graph_snapshot.attribution, line, &AttributionSnapshot::line);
      if (attribution != graph_snapshot.attribution.end()) {
        ImGui::SetTooltip("%s · %s", attribution->actor.c_str(),
                          attribution->agent ? "agent" : "human");
      }
    }
  }
  ImGui::PopFont();
}

void App::draw_diagnostics_panel(const EditorGraphSnapshot &graph_snapshot) {
  if (screenshot_mode()) {
    const auto &palette =
        applied_dark_theme_ ? design::dark_palette : design::light_palette;
    ImGui::TextUnformatted("Diagnostics (3)");
    ImGui::TextColored(imgui_color(palette.danger),
                       "error: expected ';' after return statement");
    ImGui::TextUnformatted("warning: unused result from save_app_shell_state");
    ImGui::TextDisabled("info: 47 graph queries are current");
    return;
  }
  auto visible_diagnostics = graph_snapshot.diagnostics;
  visible_diagnostics.insert(visible_diagnostics.end(),
                             graph_snapshot.build_diagnostics.begin(),
                             graph_snapshot.build_diagnostics.end());
  ImGui::Text("Diagnostics (%zu)", visible_diagnostics.size());
  if (!graph_snapshot.query_error.empty()) {
    ImGui::TextWrapped("Could not update diagnostics: %s",
                       graph_snapshot.query_error.c_str());
  }
  if (graph_snapshot.edit_to_diagnostic_ms > 0.0) {
    ImGui::SameLine();
    ImGui::TextDisabled("edit to first result %.3f ms",
                        graph_snapshot.edit_to_diagnostic_ms);
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
  if (visible_diagnostics.empty() && graph_snapshot.completions.empty() &&
      graph_snapshot.query_error.empty()) {
    ImGui::TextWrapped(
        "%s", panels_.descriptor(PanelId::diagnostics).empty_state.data());
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
  ImGui::TextUnformatted("Build, test and deploy");
  if (screenshot_mode()) {
    const auto &palette =
        applied_dark_theme_ ? design::dark_palette : design::light_palette;
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
    ImGui::TextDisabled("Deploy: no saved drips pending");
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
  if (ImGui::SmallButton("Deploy saved drips")) {
    request_deploy();
  }
  if (!snapshot.query_error.empty()) {
    ImGui::TextWrapped("Could not complete the operation: %s",
                       snapshot.query_error.c_str());
  }
  if (snapshot.build_running) {
    ImGui::SameLine();
    ImGui::TextDisabled("running");
  } else if (!snapshot.build_result.empty()) {
    ImGui::SameLine();
    ImGui::TextUnformatted(snapshot.build_result.c_str());
  }
  for (const auto &action : snapshot.build_actions) {
    ImGui::Text("%s: %s", action.identity.c_str(),
                build_action_state_name(action.state).data());
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
    if (!action.dependencies.empty()) {
      auto dependencies = std::string{};
      for (const auto &dependency : action.dependencies) {
        if (!dependencies.empty()) {
          dependencies += ", ";
        }
        dependencies += dependency;
      }
      ImGui::TextDisabled("requires %s", dependencies.c_str());
    }
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
  if (snapshot.deploy.running || !snapshot.deploy.error.empty() ||
      !snapshot.deploy.references.empty()) {
    ImGui::Separator();
    if (snapshot.deploy.running) {
      ImGui::TextUnformatted("Deploying saved drips...");
    }
    if (!snapshot.deploy.error.empty()) {
      ImGui::TextColored(ImVec4(0.75F, 0.12F, 0.12F, 1.0F), "%s",
                         snapshot.deploy.error.c_str());
    }
    if (!snapshot.deploy.references.empty()) {
      ImGui::Text("Transferred %llu objects, %llu bytes",
                  static_cast<unsigned long long>(snapshot.deploy.objects),
                  static_cast<unsigned long long>(snapshot.deploy.bytes));
    }
    for (const auto &reference : snapshot.deploy.references) {
      ImGui::Text("%s: %s", reference.name.c_str(),
                  deploy_outcome_name(reference.outcome).data());
      if (!reference.reason.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", reference.reason.c_str());
      }
    }
  }
  if (!snapshot.build_running && snapshot.build_result.empty() &&
      snapshot.build_actions.empty() && snapshot.tests.empty() &&
      snapshot.test_results.empty() && !snapshot.deploy.running &&
      snapshot.deploy.error.empty() && snapshot.deploy.references.empty() &&
      snapshot.query_error.empty()) {
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
            descriptor.icon.data(),
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
  const auto &descriptor = panels_.descriptor(id);
  const auto &state = panels_.panel_state(id);
  ImGui::PushID(panel_id_name(id).data());
  ImGui::Text("%s  %s", descriptor.icon.data(), descriptor.title.data());
  if (state.busy) {
    ImGui::SameLine();
    ImGui::TextDisabled("Working…");
  }
  const auto button_width = ImGui::CalcTextSize("...").x + design::space4;
  ImGui::SameLine(
      std::max(ImGui::GetContentRegionMax().x - button_width, 0.0F));
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
  draw_panel_header(id);
  switch (id) {
  case PanelId::repository:
    if (screenshot_mode()) {
      ImGui::TextDisabled("orama/liquify");
      ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      if (ImGui::TreeNode("src")) {
        ImGui::Selectable("app.cpp", true);
        ImGui::Selectable("app_state.cpp");
        ImGui::Selectable("editor_buffer.cpp");
        ImGui::TreePop();
      }
      ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      if (ImGui::TreeNode("docs")) {
        ImGui::Selectable("ui");
        ImGui::TreePop();
      }
      ImGui::Selectable("CMakeLists.txt");
      ImGui::Selectable("README.md");
    } else if (repository_) {
      draw_repository_tree();
    } else {
      ImGui::TextWrapped("%s", panels_.descriptor(id).empty_state.data());
    }
    break;
  case PanelId::search:
    ImGui::InputTextWithHint("##repository-search", "Search this repository",
                             &sidebar_search_);
    ImGui::InputTextWithHint("##repository-replace", "Replace with",
                             &replace_query_);
    ImGui::Checkbox("Regular expression", &find_options_.regular_expression);
    if (ImGui::Button("Find", ImVec2(-1.0F, 0.0F))) {
      if (editor_graph_) {
        const auto query = find_options_.regular_expression
                               ? "/" + sidebar_search_ + "/"
                               : "\"" + sidebar_search_ + "\"";
        editor_graph_->request_search(query);
      }
    }
    if (ImGui::Button("Replace all in repository", ImVec2(-1.0F, 0.0F)) &&
        editor_graph_) {
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
    ImGui::TextWrapped("%s", panels_.descriptor(id).empty_state.data());
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
    } else if (plugin_failures_.empty() && plugin_panels_.empty()) {
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
    const auto available = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosY(std::max(available.y * 0.28F, design::space8));
    ImGui::SetCursorPosX(std::max(available.x * 0.16F, design::space8));
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[ui_font_index_ + 3]);
    ImGui::TextUnformatted("liquify");
    ImGui::PopFont();
    ImGui::TextDisabled("The IDE for liquid");
    ImGui::Spacing();
    ImGui::TextUnformatted("Open a repository, then select a file from the "
                           "repository sidebar.");
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
      const auto &descriptor = panels_.descriptor(id);
      const auto label = std::string(descriptor.icon) + "  " +
                         std::string(descriptor.title) + "###panel-" +
                         std::string(panel_id_name(id));
      const auto flags =
          panels_.focus_requested(id) ? ImGuiTabItemFlags_SetSelected : 0;
      const auto selected = ImGui::BeginTabItem(label.c_str(), &open, flags);
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
  for (const auto id : panels_.panels_at(PanelPlacement::bottom)) {
    if (!panels_.layout(id).visible) {
      continue;
    }
    const auto selected = active == id;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Header,
                            ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    }
    const auto &descriptor = panels_.descriptor(id);
    const auto label =
        std::string(descriptor.icon) + "  " + std::string(descriptor.title);
    if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0.0F, 24.0F))) {
      panels_.focus(id);
      panels_.clear_focus_request();
      persist_shell_state();
    }
    if (selected) {
      ImGui::PopStyleColor();
    }
    ImGui::SameLine();
  }
  ImGui::NewLine();
  if (active) {
    draw_panel(*active);
    if (panels_.focus_requested(*active)) {
      panels_.clear_focus_request();
    }
  } else {
    ImGui::TextWrapped("No panel is docked in the bottom area.");
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
    const auto &descriptor = panels_.descriptor(id);
    const auto title = std::string(descriptor.title) + "###floating-" +
                       std::string(panel_id_name(id));
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
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

void App::draw_clone_dialog() {
  if (clone_dialog_open_) {
    ImGui::OpenPopup("Clone from lake");
    clone_dialog_open_ = false;
  }
  const auto center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(560.0F, 0.0F), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Clone from lake", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::TextWrapped("Clone a lake repository through liquid's drive client. "
                     "Offline failures remain available for retry.");
  ImGui::Spacing();
  ImGui::InputTextWithHint("Lake repository", "https://lake.example/repository",
                           &clone_remote_);
  ImGui::InputTextWithHint("Local folder", "/path/to/new-folder", &clone_path_);
  ImGui::Spacing();
  const auto ready = !clone_remote_.empty() && !clone_path_.empty();
  ImGui::BeginDisabled(!ready);
  if (ImGui::Button("Clone repository")) {
    begin_clone_repository(clone_remote_, clone_path_);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
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
  ImGui::TextUnformatted("Welcome to liquify");
  ImGui::TextDisabled(
      "Build software locally. Collaborate through lake when it is reachable.");
  ImGui::Spacing();

  const auto phase = first_run_.phase();
  if (phase == RepositoryLifecyclePhase::unreadable) {
    ImGui::TextUnformatted("Repository could not be opened");
    ImGui::TextWrapped("%s", first_run_.pending_path().string().c_str());
    ImGui::TextWrapped("%s", first_run_.error().c_str());
    if (ImGui::Button("Retry")) {
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
    ImGui::TextUnformatted(phase == RepositoryLifecyclePhase::offline
                               ? "Lake is unreachable - offline is normal"
                               : "Repository was not cloned");
    ImGui::TextWrapped("%s", first_run_.error().c_str());
    if (ImGui::Button("Retry clone")) {
      begin_clone_repository(first_run_.clone_remote(),
                             first_run_.pending_path());
    }
    ImGui::SameLine();
    if (ImGui::Button("Back")) {
      first_run_.return_to_welcome();
    }
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
  if (ImGui::Button("Open folder", ImVec2(150.0F, 38.0F))) {
    show_folder_dialog();
  }
  ImGui::SameLine();
  if (ImGui::Button("Clone from lake", ImVec2(150.0F, 38.0F))) {
    clone_dialog_open_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("New window", ImVec2(150.0F, 38.0F))) {
    open_new_window();
  }
  ImGui::EndDisabled();
  ImGui::Spacing();

  if (first_run_.session()) {
    ImGui::Text("Lake session: %s", first_run_.session()->actor.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", first_run_.session()->custody.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Sign out")) {
      first_run_.sign_out();
      persist_first_run();
    }
  } else {
    ImGui::BeginDisabled(identity_operation_.has_value());
    if (ImGui::Button(identity_operation_ ? "Signing in..."
                                          : "Sign in to lake")) {
      begin_sign_in();
    }
    ImGui::EndDisabled();
  }
  if (!identity_error_.empty()) {
    ImGui::TextWrapped("Could not establish a session: %s",
                       identity_error_.c_str());
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Recent repositories");
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
    ImGui::TableHeadersRow();
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
      ImGui::TextWrapped("%s", recent.last_change.c_str());
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
}

void App::draw_status_bar() {
  const auto repository =
      screenshot_mode() ? std::string{"orama/liquify"}
                        : (options_.repository_path
                               ? options_.repository_path->filename().string()
                               : std::string{"No repository"});
  ImGui::TextUnformatted(repository.c_str());
  ImGui::SameLine();
  const auto *screenshot =
      options_.screenshot_state
          ? find_screenshot_state(*options_.screenshot_state)
          : nullptr;
  ImGui::TextDisabled(screenshot != nullptr &&
                              screenshot->view == ScreenshotView::editor_view
                          ? "Editor view #32"
                      : screenshot_mode() ? "Change #31"
                                          : "Working change");
  if (editor_) {
    const auto cursor = editor_->cursor();
    ImGui::SameLine();
    ImGui::TextDisabled("Ln %zu, Col %zu", cursor.line + 1, cursor.byte + 1);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu cursors · %s: %zu · %s", editor_->cursors().size(),
                        editor_->preferences().indent_style == IndentStyle::tabs
                            ? "Tabs"
                            : "Spaces",
                        editor_->preferences().indent_size,
                        editor_->preferences().soft_wrap ? "Wrap" : "No wrap");
  }
  if (const auto *notification = panels_.latest_notification()) {
    ImGui::PushID(notification->id.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(notification->message.c_str())) {
      panels_.focus(notification->panel);
      const auto placement = panels_.layout(notification->panel).placement;
      if (placement == PanelPlacement::sidebar) {
        shell_state_.sidebar_visible = true;
      } else if (placement == PanelPlacement::bottom) {
        shell_state_.bottom_panel_visible = true;
      }
      persist_shell_state();
    }
    if (notification->active && notification->total > 0) {
      ImGui::SameLine();
      const auto fraction = static_cast<float>(notification->completed) /
                            static_cast<float>(notification->total);
      ImGui::ProgressBar(fraction, ImVec2(72.0F, 0.0F), "");
    }
    if (notification->active && notification->cancellable) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Cancel")) {
        static_cast<void>(panels_.request_cancel(notification->id));
      }
    } else if (!notification->active) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Dismiss")) {
        panels_.dismiss_notification(notification->id);
      }
    }
    ImGui::PopID();
  }
  const auto preference = options_.forced_theme.value_or(shell_state_.theme);
  const auto theme =
      preference == ThemePreference::system
          ? "Theme: system"
          : (preference == ThemePreference::dark ? "Theme: dark"
                                                 : "Theme: light");
  const auto width = ImGui::CalcTextSize(theme).x + design::space6;
  ImGui::SameLine(std::max(ImGui::GetWindowWidth() - width, 0.0F));
  if (ImGui::SmallButton(theme)) {
    cycle_theme();
  }
}

void App::draw_contents() {
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

  draw_floating_panels();
  draw_command_palette();
  draw_settings();
  draw_review_comment_popup();
  draw_structural_editing();
}

void App::render_editor() {
  if (settings_popup_visible_ || !editor_visible_ || !editor_ ||
      editor_width_ <= 0.0F || editor_height_ <= 0.0F) {
    return;
  }
  const auto maximum_scroll = editor_->line_count() > visible_lines_
                                  ? editor_->line_count() - visible_lines_
                                  : 0;
  scroll_line_ = std::min(scroll_line_, maximum_scroll);

  float old_scale_x = 1.0F;
  float old_scale_y = 1.0F;
  SDL_GetRenderScale(renderer_, &old_scale_x, &old_scale_y);
  const auto framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale;
  SDL_SetRenderScale(renderer_, framebuffer_scale.x, framebuffer_scale.y);
  const SDL_Rect clip{static_cast<int>(editor_x_), static_cast<int>(editor_y_),
                      static_cast<int>(editor_width_),
                      static_cast<int>(editor_height_)};
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
  const auto &palette =
      applied_dark_theme_ ? design::dark_palette : design::light_palette;
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
      cell_width_, horizontal_scroll_, review_lines, comment_lines);
  SDL_SetRenderClipRect(renderer_, nullptr);
  SDL_SetRenderScale(renderer_, old_scale_x, old_scale_y);
}

void App::frame() {
  poll_repository_lifecycle();
  poll_sign_in();
  poll_editor_graph();
  poll_review();
  poll_plugins();
  sync_panel_system();
  if (applied_dark_theme_ != dark_theme_active()) {
    apply_theme();
  }
  update_window_title();
  poll_multiplayer();
  if (fonts_rebuild_requested_) {
    ImGui_ImplSDLRenderer3_DestroyFontsTexture();
    load_font_atlas(display_scale_);
    ImGui_ImplSDLRenderer3_CreateFontsTexture();
    fonts_rebuild_requested_ = false;
  }
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  editor_visible_ = false;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("liquify", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);
  draw_contents();
  ImGui::End();

  ImGui::Render();
  const auto &palette =
      applied_dark_theme_ ? design::dark_palette : design::light_palette;
  SDL_SetRenderDrawColor(
      renderer_, static_cast<std::uint8_t>(palette.canvas.red * 255.0F),
      static_cast<std::uint8_t>(palette.canvas.green * 255.0F),
      static_cast<std::uint8_t>(palette.canvas.blue * 255.0F),
      static_cast<std::uint8_t>(palette.canvas.alpha * 255.0F));
  SDL_RenderClear(renderer_);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
  render_editor();
  if (screenshot_mode()) {
    ++screenshot_frames_rendered_;
    if (screenshot_frames_rendered_ >= 2) {
      capture_screenshot();
    }
  }
  SDL_RenderPresent(renderer_);
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

int App::run() {
  if (!initialized_) {
    return 1;
  }
  int frames = 0;
  while (!quit_) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        quit_ = true;
      }
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window_)) {
        quit_ = true;
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
      if (event.type == SDL_EVENT_TEXT_INPUT) {
        handle_text(event.text.text);
      }
      if (event.type == SDL_EVENT_KEY_DOWN) {
        handle_key(event.key.key, static_cast<std::uint16_t>(event.key.mod));
      }
#if defined(__APPLE__) && defined(__OBJC__)
      if (event.type == SDL_EVENT_USER) {
        const auto index = static_cast<std::size_t>(event.user.code);
        if (index > 0 && index <= native_menu_commands_.size()) {
          invoke_command(native_menu_commands_[index - 1]);
        }
      }
#endif
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
      quit_ = true;
    }
  }
  if (options_.smoke) {
    print_smoke_metrics();
  }
  return screenshot_failed_ ? 1 : 0;
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

} // namespace liquify
