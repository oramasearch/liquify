// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace liquify {

enum class CommandPlatform : std::uint8_t {
  macos,
  windows,
  linux,
};

enum class KeyModifier : std::uint8_t {
  primary = 1U << 0U,
  control = 1U << 1U,
  alt = 1U << 2U,
  shift = 1U << 3U,
};

struct KeyChord {
  std::uint8_t modifiers{};
  std::string key;

  auto operator==(const KeyChord &) const -> bool = default;
};

// Where a command's key binding is live. A chord whose command is out of scope is not consumed:
// the key reaches the focused surface instead (Enter types a newline unless the repository tree
// has focus). Named invocation, the palette and the native menus run a command in any scope.
enum class CommandScope : std::uint8_t {
  global,
  editor,
  repository,
};

struct CommandDefinition {
  std::string name;
  std::string title;
  std::string group;
  std::string description;
  std::vector<std::string> default_bindings;
  bool headless{};
  bool plugin{};
  CommandScope scope{CommandScope::global};
};

// A former id that resolves to the command that performs the same action. Aliases are found by
// name, by keymap overrides and by `liquify --command`, but never listed or searched.
struct CommandAlias {
  std::string name;
  std::string target;
};

struct CommandLocation {
  std::filesystem::path file;
  std::uint64_t line{1};
  std::uint64_t column{1};
};

struct KeymapOverride {
  std::string command;
  std::vector<std::string> bindings;
  CommandLocation location;
};

struct CommandDiagnostic {
  CommandLocation location;
  std::string message;
};

struct CommandConflict {
  std::string binding;
  std::vector<std::string> commands;
};

struct KeymapParseResult {
  std::vector<KeymapOverride> overrides;
  std::vector<CommandDiagnostic> diagnostics;
};

struct CommandSearchResult {
  std::string name;
  int score{};
  bool recent{};
};

[[nodiscard]] auto current_command_platform() -> CommandPlatform;

class CommandRegistry {
public:
  explicit CommandRegistry(
      CommandPlatform platform = current_command_platform());

  [[nodiscard]] auto add(CommandDefinition definition)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto add_alias(CommandAlias alias)
      -> std::expected<void, std::string>;
  // Resolves aliases: find("build.start") returns run.build.
  [[nodiscard]] auto find(std::string_view name) const
      -> const CommandDefinition *;
  [[nodiscard]] auto aliases() const -> std::span<const CommandAlias>;
  [[nodiscard]] auto commands() const -> std::span<const CommandDefinition>;
  [[nodiscard]] auto apply_overrides(std::span<const KeymapOverride> overrides)
      -> std::vector<CommandDiagnostic>;
  // Restores every command's default bindings, so overrides can be applied again from scratch
  // after a keymap changes (a live rebind in settings).
  void reset_bindings();
  [[nodiscard]] auto command_for(const KeyChord &chord) const
      -> const CommandDefinition *;
  // The conflict that disables `chord`, when two or more commands claim it.
  [[nodiscard]] auto conflict_for(const KeyChord &chord) const
      -> const CommandConflict *;
  [[nodiscard]] auto bindings(std::string_view name) const
      -> std::span<const KeyChord>;
  [[nodiscard]] auto binding_label(std::string_view name) const -> std::string;
  [[nodiscard]] auto search(std::string_view query) const
      -> std::vector<CommandSearchResult>;
  void record_use(std::string_view name);
  [[nodiscard]] auto recent() const -> std::span<const std::string>;
  [[nodiscard]] auto conflicts() const -> std::span<const CommandConflict>;
  [[nodiscard]] auto platform() const -> CommandPlatform;

private:
  void rebuild_conflicts();

  struct BindingSet {
    std::string name;
    std::vector<KeyChord> values;
    std::vector<KeyChord> defaults;
  };

  CommandPlatform platform_;
  std::vector<CommandDefinition> commands_;
  std::vector<CommandAlias> aliases_;
  std::vector<BindingSet> bindings_;
  std::vector<std::string> recent_;
  std::vector<CommandConflict> conflicts_;
};

[[nodiscard]] auto builtin_commands() -> std::span<const CommandDefinition>;
[[nodiscard]] auto builtin_command_aliases() -> std::span<const CommandAlias>;
// Adds every built-in command and alias; returns the first registration error.
[[nodiscard]] auto add_builtin_commands(CommandRegistry &registry)
    -> std::vector<std::string>;
[[nodiscard]] auto parse_key_chord(std::string_view text,
                                   CommandPlatform platform)
    -> std::expected<KeyChord, std::string>;
[[nodiscard]] auto display_key_chord(const KeyChord &chord,
                                     CommandPlatform platform) -> std::string;
// The chord a window key event names: `key_name` is the platform's name for the unmodified key
// ("A", "F7", "Return", "Left", "\\", ","). `primary` is Command on macOS and Control elsewhere;
// `control` is the macOS Control key and is ignored on other platforms. Returns nothing for a
// key no binding can name.
[[nodiscard]] auto key_chord_from_event(std::string_view key_name, bool primary,
                                        bool control, bool alt, bool shift,
                                        CommandPlatform platform)
    -> std::optional<KeyChord>;
// Splits a settings-surface binding ("Cmd+K, F6") into the chords it lists.
[[nodiscard]] auto split_binding_list(std::string_view text)
    -> std::vector<std::string>;
[[nodiscard]] auto parse_keymap(std::string_view text,
                                const std::filesystem::path &origin)
    -> KeymapParseResult;
[[nodiscard]] auto load_keymap(const std::filesystem::path &path)
    -> KeymapParseResult;
[[nodiscard]] auto global_settings_path() -> std::filesystem::path;
[[nodiscard]] auto
repository_settings_path(const std::filesystem::path &repository)
    -> std::filesystem::path;
[[nodiscard]] auto
format_command_diagnostic(const CommandDiagnostic &diagnostic) -> std::string;
[[nodiscard]] auto generate_command_reference() -> std::string;
[[nodiscard]] auto generate_default_keymap() -> std::string;

} // namespace liquify
