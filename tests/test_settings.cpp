// SPDX-License-Identifier: MIT

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "liquify/settings.hpp"

namespace liquify {
namespace {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view label) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                (std::string(label) + "-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        auto error = std::error_code{};
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path& {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    auto output = std::ofstream(path, std::ios::binary);
    output << text;
}

auto read_text(const std::filesystem::path& path) -> std::string {
    auto input = std::ifstream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

TEST(Settings, DefaultsKeepAiAndTelemetryOff) {
    const auto settings = Settings{};

    EXPECT_EQ(settings.theme, ThemePreference::system);
    EXPECT_EQ(settings.fonts.ui_family, "IBM Plex Sans");
    EXPECT_EQ(settings.fonts.editor_family, "IBM Plex Mono");
    EXPECT_FALSE(settings.ai.enabled);
    EXPECT_FALSE(settings.telemetry);
}

TEST(Settings, ParsesEveryGroupWithLiquidConfigurationValues) {
    const auto text = R"(liquify settings {
  theme { mode: dark }
  fonts {
    ui_family: "IBM Plex Sans"
    editor_family: "IBM Plex Mono"
    ui_size: 16
    editor_size: 12
  }
  editor { wrap: true whitespace: true format_on_save: true }
  keymap { bindings: { "build.run": "Cmd+B", "settings.open": "Cmd+," } }
  ai {
    enabled: true
    provider: anthropic
    model: "test-model"
    context_bytes: 8192
    max_tokens: 512
  }
  lake { account "work" { url: "https://lake.example.test" } }
  telemetry { enabled: true }
})";

    const auto parsed = parse_settings(text, "settings.liquid");

    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->theme, ThemePreference::dark);
    EXPECT_EQ(parsed->fonts.ui_size, 16U);
    EXPECT_EQ(parsed->fonts.editor_size, 12U);
    EXPECT_TRUE(parsed->editor.wrap);
    EXPECT_TRUE(parsed->editor.whitespace);
    EXPECT_TRUE(parsed->editor.format_on_save);
    EXPECT_EQ(parsed->keymap.at("build.run"), "Cmd+B");
    EXPECT_FALSE(parsed->ai.enabled) << "no settings file turns AI on (liquify#75)";
    EXPECT_EQ(parsed->ai.provider, "anthropic");
    EXPECT_EQ(parsed->ai.model, "test-model");
    EXPECT_EQ(parsed->ai.context_bytes, 8192U);
    EXPECT_EQ(parsed->ai.max_tokens, 512U);
    ASSERT_EQ(parsed->lake_accounts.size(), 1U);
    EXPECT_EQ(parsed->lake_accounts.front().name, "work");
    EXPECT_EQ(parsed->lake_accounts.front().url, "https://lake.example.test");
    EXPECT_TRUE(parsed->telemetry);
}

TEST(Settings, RepositoryValuesOverlayUserValues) {
    auto directory = TemporaryDirectory("liquify-settings-overlay");
    const auto user = directory.path() / "user" / "settings.liquid";
    write_text(user,
               "liquify settings {\n"
               "  theme { mode: light }\n"
               "  editor { wrap: true }\n"
               "  ai { model: \"user-model\" }\n"
               "}\n");
    write_text(repository_settings_path(directory.path()),
               "liquify settings {\n"
               "  theme { mode: dark }\n"
               "  ai { enabled: true model: \"repository-model\" }\n"
               "  lake { account \"theirs\" { url: \"https://lake.attacker.test\" } }\n"
               "}\n");

    const auto loaded = load_settings(directory.path(), user);

    EXPECT_TRUE(loaded.diagnostics.empty());
    EXPECT_EQ(loaded.settings.theme, ThemePreference::dark);
    EXPECT_TRUE(loaded.settings.editor.wrap);
    // A repository's own file cannot turn AI on, change the model, or add a Lake account
    // (liquify#75, liquify#76).
    EXPECT_FALSE(loaded.settings.ai.enabled);
    EXPECT_EQ(loaded.settings.ai.model, "user-model");
    EXPECT_TRUE(loaded.settings.lake_accounts.empty());
    EXPECT_EQ(loaded.user.theme, ThemePreference::light);
    EXPECT_FALSE(loaded.settings.telemetry);
}

TEST(Settings, ReportsLiquidParserMessageAtItsSourceLocation) {
    const auto parsed =
        parse_settings("liquify settings {\n  editor { wrap: }\n}\n", "broken-settings.liquid");

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().message,
              "broken-settings.liquid:2:18: expected literal, found '}'\n"
              "  hint: use a string, integer, boolean, version, spec, digest, "
              "path, list, or map");
}

TEST(Settings, ReportsTypedValidationAtItsSourceLocation) {
    const auto parsed =
        parse_settings("liquify settings { theme { mode: blue } }", "invalid-settings.liquid");

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().message,
              "invalid-settings.liquid:1:34: expected system, light, or dark, "
              "found identifier "
              "'blue'\n  hint: theme modes are system, light, and dark");
}

TEST(Settings, SaveRoundTripsWithoutCredentialMaterial) {
    auto directory = TemporaryDirectory("liquify-settings-save");
    auto settings = Settings{};
    settings.theme = ThemePreference::dark;
    settings.editor.whitespace = true;
    settings.keymap["build.run"] = "F6";
    settings.lake_accounts.push_back({"production", "https://lake.example.test"});
    const auto path = directory.path() / "settings.liquid";

    const auto saved = save_settings(path, settings);

    ASSERT_TRUE(saved) << saved.error().message;
    const auto text = read_text(path);
    EXPECT_EQ(text.find("token:"), std::string::npos);
    EXPECT_EQ(text.find("secret:"), std::string::npos);
    const auto reparsed = parse_settings(text, path);
    ASSERT_TRUE(reparsed) << reparsed.error().message;
    EXPECT_EQ(*reparsed, settings);
}

// liquify#76: a repository-scope save writes only what the repository overrides, never the user's
// Lake accounts, model or AI switch.
TEST(Settings, RepositoryScopeSaveWritesOnlyOverridesAndNoAccounts) {
    auto directory = TemporaryDirectory("liquify-settings-repository-save");
    auto user = Settings{};
    user.editor.wrap = true;
    user.ai.model = "user-model";
    user.lake_accounts.push_back({"production", "https://lake.example.test"});
    auto merged = user;
    merged.theme = ThemePreference::dark;
    merged.ai.enabled = true;
    const auto path = repository_settings_path(directory.path());

    const auto saved = save_settings(path, merged, SettingsScope::repository, user);

    ASSERT_TRUE(saved) << saved.error().message;
    const auto text = read_text(path);
    EXPECT_NE(text.find("theme { mode: dark }"), std::string::npos);
    EXPECT_EQ(text.find("account"), std::string::npos);
    EXPECT_EQ(text.find("lake.example.test"), std::string::npos);
    EXPECT_EQ(text.find("user-model"), std::string::npos);
    EXPECT_EQ(text.find("editor"), std::string::npos) << "the editor group is the user's own";
    EXPECT_EQ(text.find("enabled: true"), std::string::npos);
}

TEST(Settings, SaveRejectsInvalidTypedValuesWithoutReplacingTheFile) {
    auto directory = TemporaryDirectory("liquify-settings-invalid-save");
    const auto path = directory.path() / "settings.liquid";
    write_text(path, "original\n");
    auto settings = Settings{};
    settings.ai.model.clear();

    const auto saved = save_settings(path, settings);

    ASSERT_FALSE(saved);
    EXPECT_NE(saved.error().message.find("expected non-empty model name"), std::string::npos);
    EXPECT_EQ(read_text(path), "original\n");
}

TEST(Settings, ResetsOneGroupAndSearchesGroupTerms) {
    auto settings = Settings{};
    settings.theme = ThemePreference::dark;
    settings.fonts.ui_size = 20;
    settings.editor.wrap = true;
    settings.ai.enabled = true;

    reset_settings_group(settings, SettingsGroup::appearance);

    EXPECT_EQ(settings.theme, ThemePreference::system);
    EXPECT_EQ(settings.fonts.ui_size, 14U);
    EXPECT_TRUE(settings.editor.wrap);
    EXPECT_TRUE(settings.ai.enabled);
    EXPECT_TRUE(settings_group_matches(SettingsGroup::keymap, "shortcut"));
    EXPECT_TRUE(settings_group_matches(SettingsGroup::lake, "CREDENTIAL"));
    EXPECT_FALSE(settings_group_matches(SettingsGroup::telemetry, "font"));
}

} // namespace
} // namespace liquify
