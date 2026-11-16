// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "liquify/app.hpp"
#include "liquify/command.hpp"
#include "liquify/navigation.hpp"

namespace liquify {
namespace {

auto registry_for(CommandPlatform platform) -> CommandRegistry {
  auto registry = CommandRegistry(platform);
  const auto errors = add_builtin_commands(registry);
  EXPECT_TRUE(errors.empty()) << errors.front();
  return registry;
}

TEST(CommandRegistry, ContainsUniqueCommandsFromEverySurface) {
  auto registry = registry_for(CommandPlatform::macos);
  EXPECT_NE(registry.find("file.save"), nullptr);
  EXPECT_NE(registry.find("edit.command-palette"), nullptr);
  EXPECT_NE(registry.find("selection.expand"), nullptr);
  EXPECT_NE(registry.find("view.repository"), nullptr);
  EXPECT_NE(registry.find("panel.plugins"), nullptr);
  EXPECT_NE(registry.find("go.definition"), nullptr);
  EXPECT_NE(registry.find("refactor.rename"), nullptr);
  EXPECT_NE(registry.find("run.build"), nullptr);
  EXPECT_NE(registry.find("review.approve"), nullptr);
  EXPECT_NE(registry.find("assistant.ask"), nullptr);
  EXPECT_NE(registry.find("assistant.explain-diagnostic"), nullptr);
  EXPECT_NE(registry.find("help.version"), nullptr);
  EXPECT_TRUE(registry.conflicts().empty());
}

// liquify#34: one registry entry, and one palette row, per action.
TEST(CommandRegistry, ListsEveryActionOnce) {
  auto registry = registry_for(CommandPlatform::macos);
  auto titles = std::set<std::string>{};
  for (const auto &command : registry.commands()) {
    EXPECT_TRUE(titles.insert(command.title).second)
        << "two commands are titled " << command.title;
  }
  auto names = std::set<std::string>{};
  for (const auto &match : registry.search("")) {
    EXPECT_TRUE(names.insert(match.name).second) << match.name;
  }
  EXPECT_EQ(names.size(), registry.commands().size());
}

// The former panel-scoped ids resolve to the command that performs the action, for named
// invocation and keymap overrides, and never appear in search.
TEST(CommandRegistry, FormerPanelIdsResolveToTheirCommand) {
  auto registry = registry_for(CommandPlatform::macos);
  ASSERT_FALSE(registry.aliases().empty());
  for (const auto &alias : builtin_command_aliases()) {
    const auto *command = registry.find(alias.name);
    ASSERT_NE(command, nullptr) << alias.name;
    EXPECT_EQ(command->name, alias.target);
  }
  EXPECT_EQ(registry.find("build.push")->name, "run.push");
  EXPECT_EQ(registry.find("structural.expand")->name, "selection.expand");
  for (const auto &match : registry.search("push")) {
    EXPECT_NE(match.name, "build.push");
  }
  const auto overrides = std::vector<KeymapOverride>{
      {"build.start", {"F6"}, {"settings.liquid", 1, 1}}};
  EXPECT_TRUE(registry.apply_overrides(overrides).empty());
  EXPECT_EQ(registry.binding_label("run.build"), "F6");
  EXPECT_EQ(registry.binding_label("build.start"), "F6");
  registry.record_use("assistant.accept");
  ASSERT_FALSE(registry.recent().empty());
  EXPECT_EQ(registry.recent().front(), "assistant.accept-edit");
  EXPECT_FALSE(registry.add({"build.start", "Build", "Run", "", {}}));
}

TEST(CommandRegistry, FuzzySearchAndRecencyUseStableNames) {
  auto registry = registry_for(CommandPlatform::macos);
  auto matches = registry.search("cmd pal");
  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(matches.front().name, "edit.command-palette");

  registry.record_use("run.build");
  registry.record_use("file.save");
  ASSERT_EQ(registry.recent().size(), 2);
  EXPECT_EQ(registry.recent()[0], "file.save");
  EXPECT_EQ(registry.recent()[1], "run.build");
}

TEST(CommandRegistry, ParsesTypedKeymapAndAppliesOverrides) {
  constexpr auto text = R"liquid(
keymap {
    bind "edit.command-palette" {
        keys: ["Primary+K", "F6"]
    }
    bind "file.save" {
        keys: []
    }
}
)liquid";
  const auto parsed = parse_keymap(text, "settings.liquid");
  ASSERT_TRUE(parsed.diagnostics.empty())
      << format_command_diagnostic(parsed.diagnostics.front());
  ASSERT_EQ(parsed.overrides.size(), 2);

  auto registry = registry_for(CommandPlatform::macos);
  const auto diagnostics = registry.apply_overrides(parsed.overrides);
  EXPECT_TRUE(diagnostics.empty());
  EXPECT_EQ(registry.binding_label("edit.command-palette"), "Cmd+K, F6");
  EXPECT_TRUE(registry.bindings("file.save").empty());
  const auto key = parse_key_chord("Cmd+K", CommandPlatform::macos);
  ASSERT_TRUE(key);
  ASSERT_NE(registry.command_for(*key), nullptr);
  EXPECT_EQ(registry.command_for(*key)->name, "edit.command-palette");
}

TEST(CommandRegistry, ReportsKeymapTypesUnknownCommandsAndConflicts) {
  constexpr auto invalid = R"liquid(
keymap {
    bind "missing.command" {
        key: "Primary+Q"
        keys: [42]
    }
    bind "missing.valid" {
        keys: ["F6"]
    }
}
)liquid";
  const auto parsed = parse_keymap(invalid, "invalid.liquid");
  ASSERT_EQ(parsed.diagnostics.size(), 2);
  EXPECT_NE(parsed.diagnostics[0].message.find("unknown bind field"),
            std::string::npos);
  EXPECT_NE(parsed.diagnostics[1].message.find("must be strings"),
            std::string::npos);

  auto registry = registry_for(CommandPlatform::macos);
  const auto unknown = registry.apply_overrides(parsed.overrides);
  ASSERT_EQ(unknown.size(), 1);
  EXPECT_NE(unknown.front().message.find("missing.valid"), std::string::npos);
  auto plugin = CommandDefinition{"sample.reset",
                                  "Reset Sample",
                                  "Plugins",
                                  "Reset sample plugin state.",
                                  {"Primary+Shift+R"},
                                  true,
                                  true};
  ASSERT_TRUE(registry.add(std::move(plugin)));
  ASSERT_EQ(registry.conflicts().size(), 1);
  EXPECT_EQ(registry.conflicts().front().binding, "Cmd+Shift+R");
  EXPECT_EQ(registry.conflicts().front().commands.size(), 2);
  const auto chord = parse_key_chord("Primary+Shift+R", CommandPlatform::macos);
  ASSERT_TRUE(chord);
  EXPECT_EQ(registry.command_for(*chord), nullptr);
}

TEST(CommandRegistry, ReportsConflictIntroducedByUserOverride) {
  constexpr auto text = R"liquid(
keymap {
    bind "file.save" {
        keys: ["F7"]
    }
}
)liquid";
  const auto parsed = parse_keymap(text, "conflict.liquid");
  ASSERT_TRUE(parsed.diagnostics.empty());
  auto registry = registry_for(CommandPlatform::linux);
  EXPECT_TRUE(registry.apply_overrides(parsed.overrides).empty());
  ASSERT_EQ(registry.conflicts().size(), 1);
  EXPECT_EQ(registry.conflicts().front().binding, "F7");
  EXPECT_EQ(registry.conflicts().front().commands,
            (std::vector<std::string>{"file.save", "run.build"}));
}

TEST(CommandRegistry, UsesPlatformPrimaryModifier) {
  const auto mac = parse_key_chord("Primary+Shift+P", CommandPlatform::macos);
  const auto windows =
      parse_key_chord("Primary+Shift+P", CommandPlatform::windows);
  ASSERT_TRUE(mac);
  ASSERT_TRUE(windows);
  EXPECT_EQ(display_key_chord(*mac, CommandPlatform::macos), "Cmd+Shift+P");
  EXPECT_EQ(display_key_chord(*windows, CommandPlatform::windows),
            "Ctrl+Shift+P");
  EXPECT_FALSE(parse_key_chord("Cmd+P", CommandPlatform::linux));
}

TEST(CommandRegistry, ReferenceMatchesCommittedPage) {
  const auto path =
      std::filesystem::path(LIQUIFY_SOURCE_DIR) / "docs" / "ui" / "commands.md";
  auto input = std::ifstream(path, std::ios::binary);
  ASSERT_TRUE(input) << path;
  const auto committed = std::string(std::istreambuf_iterator<char>(input), {});
  EXPECT_EQ(committed, generate_command_reference());
}

TEST(CommandRegistry, DefaultKeymapMatchesCommittedFile) {
  const auto path = std::filesystem::path(LIQUIFY_SOURCE_DIR) / "docs" / "ui" /
                    "default-keymap.liquid";
  auto input = std::ifstream(path, std::ios::binary);
  ASSERT_TRUE(input) << path;
  const auto committed = std::string(std::istreambuf_iterator<char>(input), {});
  EXPECT_EQ(committed, generate_default_keymap());
  const auto parsed = parse_keymap(committed, path);
  EXPECT_TRUE(parsed.diagnostics.empty());
  EXPECT_FALSE(parsed.overrides.empty());
}

// liquify#34: the window has an implementation for every built-in command, so a palette row, a
// menu item or a key never names an action that does nothing.
TEST(CommandDispatch, EveryBuiltinCommandHasAWindowImplementation) {
  for (const auto &command : builtin_commands()) {
    EXPECT_TRUE(App::handles_command(command.name)) << command.name;
  }
  for (const auto &alias : builtin_command_aliases()) {
    EXPECT_FALSE(App::handles_command(alias.name))
        << alias.name << " runs through its target, not a second handler";
  }
  EXPECT_FALSE(App::handles_command("missing.command"));
}

// A window key event becomes the chord the registry resolves; there is no other key path.
TEST(CommandDispatch, KeyEventsResolveThroughTheRegistry) {
  auto registry = registry_for(CommandPlatform::macos);
  const auto resolve = [&registry](std::string_view key, bool primary,
                                   bool control, bool alt, bool shift)
      -> std::string {
    const auto chord = key_chord_from_event(key, primary, control, alt, shift,
                                            CommandPlatform::macos);
    if (!chord) {
      return "no chord";
    }
    const auto *command = registry.command_for(*chord);
    return command == nullptr ? "" : command->name;
  };
  EXPECT_EQ(resolve("P", true, false, false, true), "edit.command-palette");
  EXPECT_EQ(resolve("P", true, false, false, false), "go.quick-open");
  EXPECT_EQ(resolve("A", true, false, false, false), "edit.select-all");
  EXPECT_EQ(resolve("F7", false, false, false, false), "run.build");
  EXPECT_EQ(resolve("F12", false, false, false, true), "go.references");
  EXPECT_EQ(resolve("Left", false, false, true, false), "go.back");
  EXPECT_EQ(resolve("Return", false, false, false, false), "repository.open");
  EXPECT_EQ(resolve("Keypad Enter", false, false, false, false), "repository.open");
  EXPECT_EQ(resolve(".", true, false, false, true), "go.breadcrumbs");
  EXPECT_EQ(resolve("\\", true, false, false, false), "view.toggle-editor-split");
  EXPECT_EQ(resolve(",", true, false, false, false), "settings.open");
  EXPECT_EQ(resolve("Q", true, false, false, false), "app.quit");
  // The macOS Control key is its own modifier: Ctrl+A is not Cmd+A.
  EXPECT_EQ(resolve("A", false, true, false, false), "");
  EXPECT_EQ(resolve("Left Shift", false, false, false, true), "no chord");

  // On Linux and Windows `control` is the primary key and is not counted twice.
  auto linux_registry = registry_for(CommandPlatform::linux);
  const auto chord = key_chord_from_event("S", true, true, false, false,
                                          CommandPlatform::linux);
  ASSERT_TRUE(chord);
  ASSERT_NE(linux_registry.command_for(*chord), nullptr);
  EXPECT_EQ(linux_registry.command_for(*chord)->name, "file.save");
}

TEST(CommandDispatch, ScopesKeepContextualKeysOutOfTheWay) {
  auto registry = registry_for(CommandPlatform::macos);
  EXPECT_EQ(registry.find("repository.open")->scope, CommandScope::repository);
  EXPECT_EQ(registry.find("edit.select-all")->scope, CommandScope::editor);
  EXPECT_EQ(registry.find("edit.paste")->scope, CommandScope::editor);
  EXPECT_EQ(registry.find("go.quick-open")->scope, CommandScope::global);
  EXPECT_EQ(registry.find("run.build")->scope, CommandScope::global);
}

// A rebind replaces the old chord, which then resolves to nothing; resetting and reapplying (what
// a live edit in settings does) gives the same result, and the default comes back once the
// override is gone.
TEST(CommandDispatch, RebindReplacesTheOldChordAndResetsCleanly) {
  auto registry = registry_for(CommandPlatform::macos);
  const auto f6 = *parse_key_chord("F6", CommandPlatform::macos);
  const auto cmd_a = *parse_key_chord("Primary+A", CommandPlatform::macos);
  const auto overrides = std::vector<KeymapOverride>{
      {"edit.select-all", split_binding_list("F6"), {"Settings > Keymap", 1, 1}}};
  EXPECT_TRUE(registry.apply_overrides(overrides).empty());
  EXPECT_EQ(registry.command_for(f6)->name, "edit.select-all");
  EXPECT_EQ(registry.command_for(cmd_a), nullptr);
  EXPECT_EQ(registry.binding_label("edit.select-all"), "F6");

  registry.reset_bindings();
  EXPECT_TRUE(registry.apply_overrides(overrides).empty());
  EXPECT_EQ(registry.command_for(f6)->name, "edit.select-all");

  registry.reset_bindings();
  EXPECT_EQ(registry.command_for(f6), nullptr);
  EXPECT_EQ(registry.command_for(cmd_a)->name, "edit.select-all");
  EXPECT_EQ(registry.binding_label("edit.select-all"), "Cmd+A");
}

TEST(CommandDispatch, ConflictingRebindDisablesTheChordAndNamesBothCommands) {
  auto registry = registry_for(CommandPlatform::macos);
  const auto overrides = std::vector<KeymapOverride>{
      {"edit.select-all", {"Primary+L"}, {"Settings > Keymap", 1, 1}}};
  EXPECT_TRUE(registry.apply_overrides(overrides).empty());
  const auto chord = *parse_key_chord("Primary+L", CommandPlatform::macos);
  EXPECT_EQ(registry.command_for(chord), nullptr);
  const auto *conflict = registry.conflict_for(chord);
  ASSERT_NE(conflict, nullptr);
  EXPECT_EQ(conflict->binding, "Cmd+L");
  EXPECT_EQ(conflict->commands,
            (std::vector<std::string>{"edit.select-all", "selection.line"}));
  EXPECT_EQ(registry.conflict_for(*parse_key_chord("Primary+A",
                                                   CommandPlatform::macos)),
            nullptr);
  registry.reset_bindings();
  EXPECT_TRUE(registry.conflicts().empty());
}

// The settings surface's former ids still rebind the registry command they name.
TEST(CommandDispatch, SettingsSurfaceIdsResolveToRegistryCommands) {
  auto registry = registry_for(CommandPlatform::macos);
  EXPECT_EQ(registry.find("build.run")->name, "run.build");
  EXPECT_EQ(registry.find("tests.run")->name, "run.tests");
  EXPECT_EQ(registry.find("push.run")->name, "run.push");
  EXPECT_EQ(registry.find("palette.open")->name, "edit.command-palette");
  const auto overrides = std::vector<KeymapOverride>{
      {"build.run", split_binding_list("F6, Cmd+Shift+B"), {"settings", 1, 1}}};
  EXPECT_TRUE(registry.apply_overrides(overrides).empty());
  EXPECT_EQ(registry.binding_label("run.build"), "F6, Cmd+Shift+B");
}

TEST(CommandDispatch, SplitsSettingsBindingLists) {
  EXPECT_EQ(split_binding_list("F6"), (std::vector<std::string>{"F6"}));
  EXPECT_EQ(split_binding_list("F6, Cmd+K"),
            (std::vector<std::string>{"F6", "Cmd+K"}));
  EXPECT_EQ(split_binding_list("Primary+,"),
            (std::vector<std::string>{"Primary+,"}));
  EXPECT_TRUE(split_binding_list("").empty());
}

// Navigation actions are registry commands: searched, bound and shown in the reference.
TEST(CommandDispatch, NavigationActionsAreInTheRegistry) {
  auto registry = registry_for(CommandPlatform::macos);
  const auto reference = generate_command_reference();
  for (const auto &command : navigation_commands()) {
    ASSERT_NE(registry.find(command.name), nullptr) << command.name;
    EXPECT_NE(reference.find("`" + std::string(command.name) + "`"),
              std::string::npos)
        << command.name;
  }
  EXPECT_EQ(registry.binding_label("go.quick-open"), "Cmd+P");
  EXPECT_EQ(registry.binding_label("go.breadcrumbs"), "Cmd+Shift+Period");
}

} // namespace
} // namespace liquify
