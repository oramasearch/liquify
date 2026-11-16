// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/error.hpp"
#include "liquify/app_state.hpp"

namespace liquify {

enum class SettingsGroup : std::uint8_t {
    appearance,
    editor,
    keymap,
    ai,
    lake,
    telemetry,
};

enum class SettingsScope : std::uint8_t {
    user,
    repository,
};

struct FontSettings {
    std::string ui_family{"IBM Plex Sans"};
    std::string editor_family{"IBM Plex Mono"};
    std::uint32_t ui_size{14};
    std::uint32_t editor_size{14};

    auto operator==(const FontSettings&) const -> bool = default;
};

struct EditorSettings {
    bool wrap{};
    bool whitespace{};
    bool format_on_save{};

    auto operator==(const EditorSettings&) const -> bool = default;
};

struct AiSettings {
    bool enabled{};
    std::string provider{"anthropic"};
    std::string model{"claude-opus-5"};
    std::size_t context_bytes{64 * 1024};
    std::uint32_t max_tokens{4'096};

    auto operator==(const AiSettings&) const -> bool = default;
};

struct LakeAccountSettings {
    std::string name;
    std::string url;

    auto operator==(const LakeAccountSettings&) const -> bool = default;
};

struct Settings {
    ThemePreference theme{ThemePreference::system};
    FontSettings fonts;
    EditorSettings editor;
    std::map<std::string, std::string> keymap;
    AiSettings ai;
    std::vector<LakeAccountSettings> lake_accounts;
    bool telemetry{};

    auto operator==(const Settings&) const -> bool = default;
};

struct SettingsDiagnostic {
    std::filesystem::path path;
    std::uint64_t line{1};
    std::uint64_t column{1};
    std::string message;

    auto operator==(const SettingsDiagnostic&) const -> bool = default;
};

struct SettingsLoad {
    Settings settings;
    // The user file's settings alone: what a repository-scope save is compared against.
    Settings user;
    std::vector<SettingsDiagnostic> diagnostics;
};

[[nodiscard]] auto user_settings_path() -> std::filesystem::path;
[[nodiscard]] auto repository_settings_path(const std::filesystem::path& repository)
    -> std::filesystem::path;
// A settings file never turns AI on: `ai.enabled` is read from no file (liquify#75; the opt-in is
// per user and per repository, ai_config.hpp). A repository-scope file also cannot set Lake
// accounts or the AI provider and model (liquify#76); they are validated and ignored.
[[nodiscard]] auto parse_settings(std::string_view text,
                                  const std::filesystem::path& path,
                                  Settings base = {},
                                  SettingsScope scope = SettingsScope::user)
    -> liquid::result<Settings>;
[[nodiscard]] auto
load_settings(const std::optional<std::filesystem::path>& repository = std::nullopt,
              std::optional<std::filesystem::path> user_path = std::nullopt) -> SettingsLoad;
// A user-scope save writes every group except `ai.enabled`. A repository-scope save writes only the
// groups that differ from `user` (the user file's settings), and never Lake accounts, the AI
// provider or model, or `ai.enabled` (liquify#76).
[[nodiscard]] auto save_settings(const std::filesystem::path& path,
                                 const Settings& settings,
                                 SettingsScope scope = SettingsScope::user,
                                 const Settings& user = {}) -> liquid::result<void>;
void reset_settings_group(Settings& settings, SettingsGroup group);
[[nodiscard]] auto settings_group_matches(SettingsGroup group, std::string_view query) -> bool;
[[nodiscard]] auto settings_group_name(SettingsGroup group) -> std::string_view;

} // namespace liquify
