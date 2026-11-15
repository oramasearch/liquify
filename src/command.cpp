#include "liquify/command.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#include "liquid/configuration.hpp"

namespace liquify {
namespace {

using DefinitionArray = std::array<CommandDefinition, 44>;

const DefinitionArray definitions{{
    {"file.close",
     "Close File",
     "File",
     "Close the active buffer, confirming any unsaved changes.",
     {"Primary+W"}},
    {"file.save",
     "Save",
     "File",
     "Save the active editor buffer through libliquid.",
     {"Primary+S"}},
    {"edit.command-palette",
     "Command Palette",
     "Edit",
     "Find and run any registered command.",
     {"Primary+Shift+P"}},
    {"edit.select-all", "Select All", "Edit", "Select the active editor buffer.", {"Primary+A"}},
    {"edit.toggle-input-mode",
     "Toggle Input Mode",
     "Edit",
     "Switch between standard and modal input.",
     {"Primary+M"}},
    {"selection.expand",
     "Expand Syntax Selection",
     "Selection",
     "Expand selection to the next syntax ancestor.",
     {"Primary+Shift+Right"}},
    {"selection.shrink",
     "Shrink Syntax Selection",
     "Selection",
     "Return to the previous syntax selection.",
     {"Primary+Shift+Left"}},
    {"selection.enclosing-definition",
     "Select Enclosing Definition",
     "Selection",
     "Select the enclosing definition from the analysis graph.",
     {"Primary+Shift+D"}},
    {"selection.symbol-references",
     "Select Symbol References",
     "Selection",
     "Find references to the selected symbol.",
     {"Primary+Alt+F"}},
    {"view.toggle-sidebar",
     "Toggle Sidebar",
     "View",
     "Show or hide the contextual sidebar.",
     {"Primary+B"}},
    {"view.toggle-bottom-panel",
     "Toggle Bottom Panel",
     "View",
     "Show or hide the bottom panel.",
     {"Primary+J"}},
    {"view.toggle-editor-split",
     "Toggle Editor Split",
     "View",
     "Show or hide the secondary editor overview.",
     {"Primary+Backslash"}},
    {"view.cycle-theme",
     "Cycle Color Theme",
     "View",
     "Cycle system, light, and dark theme preferences.",
     {"Primary+Shift+T"}},
    {"view.repository", "Show Repository", "View", "Open the Repository sidebar.", {}},
    {"view.search", "Show Search", "View", "Open the Search sidebar.", {}},
    {"view.review", "Show Review", "View", "Open the Review sidebar.", {}},
    {"view.build", "Show Build", "View", "Open the Build sidebar.", {}},
    {"view.multiplayer", "Show Multiplayer", "View", "Open the Multiplayer sidebar.", {}},
    {"panel.diagnostics", "Show Diagnostics Panel", "View", "Open the Diagnostics panel.", {}},
    {"panel.build", "Show Build Panel", "View", "Open the build, test, and push panel.", {}},
    {"panel.assistant", "Show Assistant Panel", "View", "Open the Assistant panel.", {}},
    {"panel.plugins", "Show Plugins Panel", "View", "Open the Plugins panel.", {}},
    {"go.definition", "Go to Definition", "Go", "Open the selected symbol definition.", {"F12"}},
    {"go.hover", "Show Hover", "Go", "Show graph-backed hover information.", {"F1"}},
    {"go.completion",
     "Show Completions",
     "Go",
     "Request graph-backed completions.",
     {"Primary+Space"}},
    {"refactor.rename",
     "Rename Symbol",
     "Refactor",
     "Preview a cross-file symbol rename.",
     {"Primary+Shift+R"}},
    {"refactor.extract-function",
     "Extract Function",
     "Refactor",
     "Preview a C or C++ function extraction.",
     {"Primary+Shift+X"}},
    {"run.build", "Build", "Run", "Plan and start the configured build.", {"F7"}},
    {"run.tests", "Run All Tests", "Run", "List and run all configured test actions.", {"F8"}},
    {"run.push", "Push", "Run", "Push changes to the lake this repository came from.", {"F9"}},
    {"run.set-lake",
     "Set Lake",
     "Run",
     "Choose the lake repository this one pushes to.",
     {}},
    {"change.cut",
     "Cut Change",
     "Change",
     "Name the drips saved since the last change as one change.",
     {"Primary+Shift+K"}},
    {"review.next-hunk", "Next Hunk", "Review", "Move to the next review hunk.", {"Alt+Down"}},
    {"review.previous-hunk",
     "Previous Hunk",
     "Review",
     "Move to the previous review hunk.",
     {"Alt+Up"}},
    {"review.next-comment",
     "Next Comment",
     "Review",
     "Move to the next review comment.",
     {"Alt+C"}},
    {"review.new-comment",
     "New Comment",
     "Review",
     "Create a cursor-anchored comment.",
     {"Primary+Alt+C"}},
    {"review.approve", "Approve", "Review", "Approve the open review.", {"Primary+Alt+A"}},
    {"review.request",
     "Request Review",
     "Review",
     "Ask people on lake to review the current change.",
     {}},
    {"assistant.ask",
     "Ask Assistant",
     "Assistant",
     "Ask with inspectable graph context.",
     {"Primary+Shift+A"}},
    {"assistant.explain-diagnostic",
     "Explain Diagnostic",
     "Assistant",
     "Explain the active diagnostic.",
     {"Primary+Shift+E"}},
    {"assistant.propose-edit",
     "Propose Edit",
     "Assistant",
     "Request an edit preview without changing the repository.",
     {"Primary+Shift+I"}},
    {"assistant.accept-edit",
     "Accept Edit",
     "Assistant",
     "Accept the current assistant edit preview.",
     {"Primary+Enter"}},
    {"assistant.reject-edit",
     "Reject Edit",
     "Assistant",
     "Reject the current assistant edit preview.",
     {"Primary+Backspace"}},
    {"assistant.toggle",
     "Toggle Assistant",
     "Assistant",
     "Turn the assistant on or off.",
     {"Primary+Shift+L"}},
}};

const std::array<CommandDefinition, 27> trailing_definitions{{
    {"edit.find",
     "Find",
     "Edit",
     "Find text in the active editor.",
     {"Primary+F"}},
    {"edit.focus-search",
     "Focus Search",
     "Edit",
     "Focus the repository search panel.",
     {"Primary+Shift+F"}},
    {"edit.undo",
     "Undo",
     "Edit",
     "Undo the latest editor operation.",
     {"Primary+Z"}},
    {"edit.redo",
     "Redo",
     "Edit",
     "Redo the latest editor operation.",
     {"Primary+Shift+Z"}},
    {"selection.line",
     "Select Line",
     "Selection",
     "Select the active editor line.",
     {"Primary+L"}},
    {"selection.add-cursor-up",
     "Add Cursor Above",
     "Selection",
     "Add a cursor above the active editor cursor.",
     {"Primary+Alt+Up"}},
    {"selection.add-cursor-down",
     "Add Cursor Below",
     "Selection",
     "Add a cursor below the active editor cursor.",
     {"Primary+Alt+Down"}},
    {"review.refresh",
     "Refresh Review",
     "Review",
     "Refresh the open review.",
     {}},
    {"panel.reset-layout",
     "Reset Panel Layout",
     "View",
     "Restore the default panel layout.",
     {}},
    {"settings.open",
     "Open Settings",
     "Preferences",
     "Open the live settings surface.",
     {"Primary+Comma"}},
    {"build.start",
     "Build",
     "Build, test and push",
     "Start the configured build from the build panel.",
     {}},
    {"build.test",
     "Run All Tests",
     "Build, test and push",
     "Run all configured test actions from the build panel.",
     {}},
    {"build.push",
     "Push",
     "Build, test and push",
     "Push changes from the build panel.",
     {}},
    {"assistant.explain",
     "Explain Diagnostic",
     "Assistant",
     "Explain the active diagnostic from the assistant panel.",
     {}},
    {"assistant.propose",
     "Propose Edit",
     "Assistant",
     "Request an edit preview from the assistant panel.",
     {}},
    {"assistant.accept",
     "Accept Edit",
     "Assistant",
     "Accept the assistant edit preview from the assistant panel.",
     {}},
    {"assistant.reject",
     "Reject Edit",
     "Assistant",
     "Reject the assistant edit preview from the assistant panel.",
     {}},
    {"assistant.context",
     "Preview Assistant Context",
     "Assistant",
     "Show the assistant context from the assistant panel.",
     {}},
    {"structural.expand",
     "Expand Syntax Selection",
     "Structural editing",
     "Expand selection from the structural editing panel.",
     {}},
    {"structural.shrink",
     "Shrink Syntax Selection",
     "Structural editing",
     "Shrink selection from the structural editing panel.",
     {}},
    {"structural.definition",
     "Select Enclosing Definition",
     "Structural editing",
     "Select the enclosing definition from the structural editing panel.",
     {}},
    {"structural.references",
     "Select Symbol References",
     "Structural editing",
     "Find symbol references from the structural editing panel.",
     {}},
    {"structural.rename",
     "Rename Symbol",
     "Structural editing",
     "Preview a symbol rename from the structural editing panel.",
     {}},
    {"structural.extract",
     "Extract Function",
     "Structural editing",
     "Preview function extraction from the structural editing panel.",
     {}},
    {"assistant.preview-context",
     "Preview Assistant Context",
     "Assistant",
     "Show exactly what will be sent before a request.",
     {"Primary+Shift+C"}},
    {"help.version",
     "Show Version",
     "Help",
     "Print the liquify and libliquid versions.",
     {},
     true},
    {"help.command-reference",
     "Command Reference",
     "Help",
     "Generate the command registry reference.",
     {},
     true},
}};

auto lowercase(std::string_view value) -> std::string {
  auto output = std::string(value);
  std::ranges::transform(output, output.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return output;
}

auto trim(std::string_view value) -> std::string_view {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return value;
}

auto modifier(KeyModifier value) -> std::uint8_t {
  return static_cast<std::uint8_t>(value);
}

auto has_modifier(const KeyChord &chord, KeyModifier value) -> bool {
  return (chord.modifiers & modifier(value)) != 0;
}

auto canonical_key(std::string_view key) -> std::optional<std::string> {
  const auto lowered = lowercase(trim(key));
  if (lowered.size() == 1 &&
      std::isalnum(static_cast<unsigned char>(lowered.front())) != 0) {
    return std::string(1, static_cast<char>(std::toupper(lowered.front())));
  }
  static const auto names = std::map<std::string_view, std::string_view>{
      {"backslash", "Backslash"},
      {"\\", "Backslash"},
      {"comma", "Comma"},
      {",", "Comma"},
      {"backspace", "Backspace"},
      {"delete", "Delete"},
      {"down", "Down"},
      {"end", "End"},
      {"enter", "Enter"},
      {"escape", "Escape"},
      {"home", "Home"},
      {"left", "Left"},
      {"pagedown", "PageDown"},
      {"pageup", "PageUp"},
      {"return", "Enter"},
      {"right", "Right"},
      {"space", "Space"},
      {"tab", "Tab"},
      {"up", "Up"},
  };
  if (const auto found = names.find(lowered); found != names.end()) {
    return std::string(found->second);
  }
  if (lowered.size() >= 2 && lowered.front() == 'f') {
    const auto number = lowered.substr(1);
    if (std::ranges::all_of(number, [](unsigned char character) {
          return std::isdigit(character) != 0;
        })) {
      auto value = 0;
      const auto parsed =
          std::from_chars(number.data(), number.data() + number.size(), value);
      if (parsed.ec == std::errc{} &&
          parsed.ptr == number.data() + number.size() && value >= 1 &&
          value <= 24) {
        return "F" + std::to_string(value);
      }
    }
  }
  return std::nullopt;
}

auto split_binding(std::string_view text) -> std::vector<std::string_view> {
  auto parts = std::vector<std::string_view>{};
  while (!text.empty()) {
    const auto plus = text.find('+');
    parts.push_back(trim(text.substr(0, plus)));
    if (plus == std::string_view::npos) {
      break;
    }
    text.remove_prefix(plus + 1);
  }
  return parts;
}

auto score_match(std::string_view query, const CommandDefinition &command)
    -> int {
  auto needle = lowercase(query);
  auto haystack =
      lowercase(command.title + " " + command.name + " " + command.group);
  needle.erase(std::remove_if(needle.begin(), needle.end(),
                              [](unsigned char character) {
                                return std::isspace(character) != 0;
                              }),
               needle.end());
  haystack.erase(std::remove_if(haystack.begin(), haystack.end(),
                                [](unsigned char character) {
                                  return std::isspace(character) != 0;
                                }),
                 haystack.end());
  if (needle.empty()) {
    return 1;
  }
  if (const auto exact = haystack.find(needle); exact != std::string::npos) {
    return 1'000 - static_cast<int>(exact);
  }
  auto score = 0;
  auto cursor = std::size_t{};
  auto previous = std::size_t{};
  auto matched = false;
  for (const auto character : needle) {
    const auto found = haystack.find(character, cursor);
    if (found == std::string::npos) {
      return -1;
    }
    score += 10;
    if (matched && found == previous + 1) {
      score += 12;
    }
    if (found == 0 ||
        !std::isalnum(static_cast<unsigned char>(haystack[found - 1]))) {
      score += 8;
    }
    previous = found;
    cursor = found + 1;
    matched = true;
  }
  return score - static_cast<int>(haystack.size() - needle.size());
}

auto command_location(const liquid::configuration_location &location)
    -> CommandLocation {
  return {location.file, location.line, location.column};
}

void add_diagnostic(KeymapParseResult &output,
                    const liquid::configuration_location &location,
                    std::string message) {
  output.diagnostics.push_back(
      {command_location(location), std::move(message)});
}

auto keymap_field(const liquid::configuration_record &record,
                  std::string_view name)
    -> const liquid::configuration_field * {
  const auto found = std::ranges::find(record.fields, name,
                                       &liquid::configuration_field::name);
  return found == record.fields.end() ? nullptr : &*found;
}

auto read_file(const std::filesystem::path &path)
    -> std::expected<std::string, std::string> {
  auto input = std::ifstream(path, std::ios::binary);
  if (!input) {
    return std::unexpected("cannot read settings file");
  }
  return std::string(std::istreambuf_iterator<char>(input), {});
}

auto markdown_escape(std::string_view value) -> std::string {
  auto output = std::string{};
  output.reserve(value.size());
  for (const auto character : value) {
    if (character == '|') {
      output += "\\|";
    } else {
      output += character;
    }
  }
  return output;
}

auto default_label(const CommandDefinition &command, CommandPlatform platform)
    -> std::string {
  auto labels = std::vector<std::string>{};
  for (const auto &text : command.default_bindings) {
    const auto parsed = parse_key_chord(text, platform);
    if (parsed) {
      labels.push_back(display_key_chord(*parsed, platform));
    }
  }
  auto output = std::string{};
  for (std::size_t index = 0; index < labels.size(); ++index) {
    if (index != 0) {
      output += ", ";
    }
    output += labels[index];
  }
  return output.empty() ? "—" : output;
}

} // namespace

CommandRegistry::CommandRegistry(CommandPlatform platform)
    : platform_(platform) {}

auto CommandRegistry::add(CommandDefinition definition)
    -> std::expected<void, std::string> {
  if (definition.name.empty() || definition.title.empty() ||
      definition.group.empty()) {
    return std::unexpected("command name, title, and group are required");
  }
  if (find(definition.name) != nullptr) {
    return std::unexpected("command is already registered: " + definition.name);
  }
  auto active = std::vector<KeyChord>{};
  for (const auto &binding : definition.default_bindings) {
    auto parsed = parse_key_chord(binding, platform_);
    if (!parsed) {
      return std::unexpected("invalid default binding for " + definition.name +
                             ": " + parsed.error());
    }
    active.push_back(std::move(*parsed));
  }
  bindings_.push_back({definition.name, std::move(active)});
  commands_.push_back(std::move(definition));
  rebuild_conflicts();
  return {};
}

auto CommandRegistry::find(std::string_view name) const
    -> const CommandDefinition * {
  const auto found =
      std::ranges::find(commands_, name, &CommandDefinition::name);
  return found == commands_.end() ? nullptr : &*found;
}

auto CommandRegistry::commands() const -> std::span<const CommandDefinition> {
  return commands_;
}

auto CommandRegistry::apply_overrides(std::span<const KeymapOverride> overrides)
    -> std::vector<CommandDiagnostic> {
  auto diagnostics = std::vector<CommandDiagnostic>{};
  for (const auto &override : overrides) {
    const auto command = find(override.command);
    if (command == nullptr) {
      diagnostics.push_back(
          {override.location,
           "keymap command is not registered: " + override.command});
      continue;
    }
    auto parsed = std::vector<KeyChord>{};
    auto valid = true;
    for (const auto &binding : override.bindings) {
      auto chord = parse_key_chord(binding, platform_);
      if (!chord) {
        diagnostics.push_back(
            {override.location,
             "invalid binding '" + binding + "': " + chord.error()});
        valid = false;
      } else if (std::ranges::find(parsed, *chord) == parsed.end()) {
        parsed.push_back(std::move(*chord));
      }
    }
    if (!valid) {
      continue;
    }
    auto found =
        std::ranges::find(bindings_, override.command, &BindingSet::name);
    found->values = std::move(parsed);
  }
  rebuild_conflicts();
  return diagnostics;
}

auto CommandRegistry::command_for(const KeyChord &chord) const
    -> const CommandDefinition * {
  const auto conflict = std::ranges::find_if(conflicts_, [&](const auto &item) {
    const auto parsed = parse_key_chord(item.binding, platform_);
    return parsed && *parsed == chord;
  });
  if (conflict != conflicts_.end()) {
    return nullptr;
  }
  for (const auto &set : bindings_) {
    if (std::ranges::find(set.values, chord) != set.values.end()) {
      return find(set.name);
    }
  }
  return nullptr;
}

auto CommandRegistry::bindings(std::string_view name) const
    -> std::span<const KeyChord> {
  const auto found = std::ranges::find(bindings_, name, &BindingSet::name);
  if (found == bindings_.end()) {
    return {};
  }
  return found->values;
}

auto CommandRegistry::binding_label(std::string_view name) const
    -> std::string {
  auto output = std::string{};
  for (const auto &chord : bindings(name)) {
    if (!output.empty()) {
      output += ", ";
    }
    output += display_key_chord(chord, platform_);
  }
  return output;
}

auto CommandRegistry::search(std::string_view query) const
    -> std::vector<CommandSearchResult> {
  auto output = std::vector<CommandSearchResult>{};
  for (const auto &command : commands_) {
    const auto score = score_match(query, command);
    if (score < 0) {
      continue;
    }
    output.push_back(
        {command.name, score,
         std::ranges::find(recent_, command.name) != recent_.end()});
  }
  std::ranges::stable_sort(output, [&](const auto &left, const auto &right) {
    if (left.score != right.score) {
      return left.score > right.score;
    }
    const auto left_recent = std::ranges::find(recent_, left.name);
    const auto right_recent = std::ranges::find(recent_, right.name);
    if (left_recent != recent_.end() || right_recent != recent_.end()) {
      return left_recent < right_recent;
    }
    return find(left.name)->title < find(right.name)->title;
  });
  return output;
}

void CommandRegistry::record_use(std::string_view name) {
  if (find(name) == nullptr) {
    return;
  }
  recent_.erase(std::remove(recent_.begin(), recent_.end(), name),
                recent_.end());
  recent_.insert(recent_.begin(), std::string(name));
  if (recent_.size() > 8) {
    recent_.resize(8);
  }
}

auto CommandRegistry::recent() const -> std::span<const std::string> {
  return recent_;
}

auto CommandRegistry::conflicts() const -> std::span<const CommandConflict> {
  return conflicts_;
}

auto CommandRegistry::platform() const -> CommandPlatform { return platform_; }

void CommandRegistry::rebuild_conflicts() {
  auto owners = std::map<std::string, std::vector<std::string>>{};
  for (const auto &set : bindings_) {
    for (const auto &chord : set.values) {
      owners[display_key_chord(chord, platform_)].push_back(set.name);
    }
  }
  conflicts_.clear();
  for (auto &[binding, commands] : owners) {
    if (commands.size() > 1) {
      conflicts_.push_back({std::move(binding), std::move(commands)});
    }
  }
}

auto current_command_platform() -> CommandPlatform {
#if defined(__APPLE__)
  return CommandPlatform::macos;
#elif defined(_WIN32)
  return CommandPlatform::windows;
#else
  return CommandPlatform::linux;
#endif
}

auto builtin_commands() -> std::span<const CommandDefinition> {
  static const auto all = [] {
    auto output =
        std::vector<CommandDefinition>(definitions.begin(), definitions.end());
    output.insert(output.end(), trailing_definitions.begin(),
                  trailing_definitions.end());
    return output;
  }();
  return all;
}

auto parse_key_chord(std::string_view text, CommandPlatform platform)
    -> std::expected<KeyChord, std::string> {
  const auto parts = split_binding(text);
  if (parts.empty() || parts.back().empty()) {
    return std::unexpected("binding requires a key");
  }
  auto output = KeyChord{};
  for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
    const auto part = lowercase(parts[index]);
    auto bit = std::uint8_t{};
    if (part == "primary") {
      bit = modifier(KeyModifier::primary);
    } else if (part == "shift") {
      bit = modifier(KeyModifier::shift);
    } else if (part == "alt" || part == "option") {
      bit = modifier(KeyModifier::alt);
    } else if (part == "ctrl" || part == "control") {
      bit = platform == CommandPlatform::macos ? modifier(KeyModifier::control)
                                               : modifier(KeyModifier::primary);
    } else if (part == "cmd" || part == "command" || part == "meta") {
      if (platform != CommandPlatform::macos) {
        return std::unexpected(
            "Command is available only on macOS; use Primary");
      }
      bit = modifier(KeyModifier::primary);
    } else {
      return std::unexpected("unknown modifier '" + std::string(parts[index]) +
                             "'");
    }
    if ((output.modifiers & bit) != 0) {
      return std::unexpected("duplicate modifier '" +
                             std::string(parts[index]) + "'");
    }
    output.modifiers |= bit;
  }
  auto key = canonical_key(parts.back());
  if (!key) {
    return std::unexpected("unknown key '" + std::string(parts.back()) + "'");
  }
  output.key = std::move(*key);
  return output;
}

auto display_key_chord(const KeyChord &chord, CommandPlatform platform)
    -> std::string {
  auto output = std::string{};
  const auto append = [&output](std::string_view part) {
    if (!output.empty()) {
      output += "+";
    }
    output += part;
  };
  if (has_modifier(chord, KeyModifier::control)) {
    append("Ctrl");
  }
  if (has_modifier(chord, KeyModifier::primary)) {
    append(platform == CommandPlatform::macos ? "Cmd" : "Ctrl");
  }
  if (has_modifier(chord, KeyModifier::alt)) {
    append(platform == CommandPlatform::macos ? "Option" : "Alt");
  }
  if (has_modifier(chord, KeyModifier::shift)) {
    append("Shift");
  }
  append(chord.key == "Backslash" ? "\\" : chord.key);
  return output;
}

auto parse_keymap(std::string_view text, const std::filesystem::path &origin)
    -> KeymapParseResult {
  auto output = KeymapParseResult{};
  auto parsed = liquid::parse_configuration(text, origin);
  if (!parsed) {
    output.diagnostics.push_back({{origin, 1, 1}, parsed.error().message});
    if (!parsed.error().context.empty()) {
      output.diagnostics.back().message += ": " + parsed.error().context;
    }
    return output;
  }
  auto seen = std::set<std::string>{};
  for (const auto &record : parsed->records) {
    if (record.kind != "keymap") {
      if (record.kind == "bind") {
        add_diagnostic(output, record.location,
                       "bind records must be inside keymap");
      }
      continue;
    }
    if (record.name) {
      add_diagnostic(output, record.location, "keymap does not take a name");
    }
    if (!record.fields.empty()) {
      add_diagnostic(output, record.fields.front().location,
                     "keymap contains bind records, not fields");
    }
    for (const auto &binding : record.records) {
      if (binding.kind != "bind") {
        add_diagnostic(output, binding.location,
                       "expected bind record inside keymap, found " +
                           binding.kind);
        continue;
      }
      if (!binding.name || binding.name->empty()) {
        add_diagnostic(output, binding.location,
                       "bind requires a command name");
        continue;
      }
      if (!seen.insert(*binding.name).second) {
        add_diagnostic(output, binding.location,
                       "duplicate keymap override for " + *binding.name);
        continue;
      }
      if (!binding.records.empty()) {
        add_diagnostic(output, binding.records.front().location,
                       "bind accepts fields only");
      }
      for (const auto &field : binding.fields) {
        if (field.name != "keys") {
          add_diagnostic(output, field.location,
                         "unknown bind field '" + field.name +
                             "'; expected keys");
        }
      }
      const auto *keys = keymap_field(binding, "keys");
      if (keys == nullptr) {
        add_diagnostic(output, binding.location,
                       "bind requires keys: [string, ...]");
        continue;
      }
      if (keys->value.kind != liquid::configuration_value_kind::list) {
        add_diagnostic(output, keys->value.location,
                       "bind keys must be a list of strings");
        continue;
      }
      auto override =
          KeymapOverride{*binding.name, {}, command_location(binding.location)};
      auto valid = true;
      for (const auto &value : keys->value.list) {
        if (value.kind != liquid::configuration_value_kind::string) {
          add_diagnostic(output, value.location,
                         "bind keys entries must be strings");
          valid = false;
        } else {
          override.bindings.push_back(value.text);
        }
      }
      if (valid) {
        output.overrides.push_back(std::move(override));
      }
    }
  }
  return output;
}

auto load_keymap(const std::filesystem::path &path) -> KeymapParseResult {
  if (path.empty() || !std::filesystem::exists(path)) {
    return {};
  }
  auto text = read_file(path);
  if (!text) {
    return {{}, {{{path, 1, 1}, text.error()}}};
  }
  return parse_keymap(*text, path);
}

auto global_settings_path() -> std::filesystem::path {
#ifdef _WIN32
  if (const auto *app_data = std::getenv("APPDATA")) {
    return std::filesystem::path(app_data) / "liquify" / "settings.liquid";
  }
#elif defined(__APPLE__)
  if (const auto *user_home = std::getenv("HOME")) {
    return std::filesystem::path(user_home) / "Library" /
           "Application Support" / "liquify" / "settings.liquid";
  }
#else
  if (const auto *xdg_config = std::getenv("XDG_CONFIG_HOME")) {
    return std::filesystem::path(xdg_config) / "liquify" / "settings.liquid";
  }
  if (const auto *user_home = std::getenv("HOME")) {
    return std::filesystem::path(user_home) / ".config" / "liquify" /
           "settings.liquid";
  }
#endif
  return {};
}

auto format_command_diagnostic(const CommandDiagnostic &diagnostic)
    -> std::string {
  return diagnostic.location.file.generic_string() + ":" +
         std::to_string(diagnostic.location.line) + ":" +
         std::to_string(diagnostic.location.column) + ": " + diagnostic.message;
}

auto generate_command_reference() -> std::string {
  auto output = std::ostringstream{};
  output << "# Command reference\n\n";
  output << "Generated from the built-in command registry by `liquify docs "
            "commands`. "
            "Do not edit the table by hand.\n\n";
  output
      << "| Group | Command | Name | macOS | Windows | Linux | Description |\n";
  output << "|---|---|---|---|---|---|---|\n";
  for (const auto &command : builtin_commands()) {
    output << "| " << markdown_escape(command.group) << " | "
           << markdown_escape(command.title) << " | `" << command.name
           << "` | `" << default_label(command, CommandPlatform::macos)
           << "` | `" << default_label(command, CommandPlatform::windows)
           << "` | `" << default_label(command, CommandPlatform::linux)
           << "` | " << markdown_escape(command.description) << " |\n";
  }
  output << "\nPlugin commands are appended at runtime under the Plugins "
            "group. Their ABI-declared "
            "default binding is resolved through the same keymap and conflict "
            "checks.\n\n";
  output << "## Palette and native menus\n\n";
  output << "`Cmd+Shift+P` on macOS and `Ctrl+Shift+P` elsewhere opens the "
            "palette. Search "
            "uses case-insensitive fuzzy subsequence matching over title, "
            "stable name, and group. "
            "The palette shows the resolved platform binding and keeps the "
            "eight most recently "
            "invoked commands at the top while the query is empty.\n\n";
  output << "On macOS the native menu bar is built from the same registry "
            "groups and active "
            "bindings. Plugin commands therefore enter the palette and the "
            "Plugins menu through "
            "one registration path.\n\n";
  output << "## Typed keymap overrides\n\n";
  output << "The global settings file is `~/Library/Application "
            "Support/liquify/settings.liquid` "
            "on macOS, `%APPDATA%\\liquify\\settings.liquid` on Windows, and "
            "`${XDG_CONFIG_HOME:-~/.config}/liquify/settings.liquid` on Linux. "
            "A repository may "
            "override it with `.liquid/settings.liquid`; repository records "
            "are applied last. "
            "The settings surface tracked by issue #37 reads these same "
            "files.\n\n";
  output << "liquify parses the files with liquid's typed `.liquid` parser, "
            "then checks the "
            "keymap-owned `keymap` and `bind` records. Other top-level records "
            "remain available "
            "to settings groups. Errors retain file, line, and column. Each "
            "override replaces a "
            "command's complete key list; an empty list unbinds it.\n\n";
  output << "```liquid\n";
  output << "keymap {\n";
  output << "    bind \"edit.command-palette\" {\n";
  output << "        keys: [\"Primary+Shift+P\"]\n";
  output << "    }\n";
  output << "}\n";
  output << "```\n";
  output << "\n`Primary` resolves to Command on macOS and Control on Windows "
            "and Linux. Explicit "
            "`Shift`, `Alt`/`Option`, and `Ctrl` modifiers are accepted. "
            "Duplicate active chords "
            "are reported with every owning command and are disabled until the "
            "conflict is "
            "removed; named invocation remains available. The generated "
            "complete default file is "
            "[`default-keymap.liquid`](default-keymap.liquid).\n\n";
  output << "## Headless commands and generation\n\n";
  output << "`liquify --command <name>` resolves the same stable registry name "
            "without opening "
            "SDL. `help.version` and `help.command-reference` are built-in "
            "headless commands. "
            "Globally configured plugin commands receive an empty "
            "canonical-CBOR map and write "
            "their canonical-CBOR result to standard output. Commands that "
            "require live editor "
            "state fail with a clear message. Unknown names exit 2.\n\n";
  output << "Regenerate and verify the committed artifacts with:\n\n";
  output << "```sh\n";
  output << "liquify docs commands --out docs/ui/commands.md\n";
  output << "liquify docs keymap --out docs/ui/default-keymap.liquid\n";
  output << "liquify docs commands --check docs/ui/commands.md\n";
  output << "liquify docs keymap --check docs/ui/default-keymap.liquid\n";
  output << "```\n";
  return output.str();
}

auto generate_default_keymap() -> std::string {
  auto output = std::ostringstream{};
  output << "# Generated from the built-in registry by liquify docs keymap.\n";
  output << "# Copy individual bind records into settings.liquid to override "
            "them.\n\n";
  output << "keymap {\n";
  for (const auto &command : builtin_commands()) {
    if (command.default_bindings.empty()) {
      continue;
    }
    output << "    bind \"" << command.name << "\" {\n";
    output << "        keys: [";
    for (std::size_t index = 0; index < command.default_bindings.size();
         ++index) {
      if (index != 0) {
        output << ", ";
      }
      output << "\"" << command.default_bindings[index] << "\"";
    }
    output << "]\n";
    output << "    }\n";
  }
  output << "}\n";
  return output.str();
}

} // namespace liquify
