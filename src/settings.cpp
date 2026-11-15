// SPDX-License-Identifier: MIT

#include "liquify/settings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <ranges>
#include <span>
#include <sstream>
#include <utility>

#include "liquid/configuration.hpp"

namespace liquify {
namespace {

using Value = liquid::configuration_value;
using ValueKind = liquid::configuration_value_kind;
using Record = liquid::configuration_record;

auto settings_error(const liquid::configuration_location& location,
                    std::string expected,
                    std::string found,
                    std::string hint) -> liquid::error {
    auto message = location.file.generic_string() + ":" + std::to_string(location.line) + ":" +
                   std::to_string(location.column) + ": expected " + std::move(expected) +
                   ", found " + std::move(found);
    if (!hint.empty()) {
        message += "\n  hint: " + std::move(hint);
    }
    return {liquid::errc::invalid_argument, std::move(message), "liquify settings"};
}

auto describe(const Value& value) -> std::string {
    switch (value.kind) {
    case ValueKind::string:
        return "string '" + value.text + "'";
    case ValueKind::integer:
        return "integer " + std::to_string(value.integer);
    case ValueKind::boolean:
        return value.boolean ? "boolean true" : "boolean false";
    case ValueKind::identifier:
        return "identifier '" + value.text + "'";
    case ValueKind::list:
        return "list";
    case ValueKind::map:
        return "map";
    case ValueKind::version:
        return "version '" + value.text + "'";
    case ValueKind::spec:
        return "spec '" + value.text + "'";
    case ValueKind::digest:
        return "digest '" + value.text + "'";
    case ValueKind::path:
        return "path '" + value.text + "'";
    }
    return "value";
}

auto field(const Record& record, std::string_view name) -> const liquid::configuration_field* {
    const auto found = std::ranges::find(record.fields, name, &liquid::configuration_field::name);
    return found == record.fields.end() ? nullptr : &*found;
}

auto boolean_field(const Record& record, std::string_view name)
    -> liquid::result<std::optional<bool>> {
    const auto* item = field(record, name);
    if (item == nullptr) {
        return std::nullopt;
    }
    if (item->value.kind != ValueKind::boolean) {
        return std::unexpected(settings_error(
            item->value.location, "true or false", describe(item->value), "booleans are unquoted"));
    }
    return item->value.boolean;
}

auto string_field(const Record& record, std::string_view name)
    -> liquid::result<std::optional<std::string>> {
    const auto* item = field(record, name);
    if (item == nullptr) {
        return std::nullopt;
    }
    if (item->value.kind != ValueKind::string) {
        return std::unexpected(settings_error(item->value.location,
                                              "string",
                                              describe(item->value),
                                              "strings are quoted: \"value\""));
    }
    return item->value.text;
}

auto identifier_field(const Record& record, std::string_view name)
    -> liquid::result<std::optional<std::string>> {
    const auto* item = field(record, name);
    if (item == nullptr) {
        return std::nullopt;
    }
    if (item->value.kind != ValueKind::identifier) {
        return std::unexpected(settings_error(
            item->value.location, "identifier", describe(item->value), "enum values are unquoted"));
    }
    return item->value.text;
}

template <class Integer>
auto integer_field(const Record& record, std::string_view name, Integer minimum, Integer maximum)
    -> liquid::result<std::optional<Integer>> {
    const auto* item = field(record, name);
    if (item == nullptr) {
        return std::nullopt;
    }
    if (item->value.kind != ValueKind::integer || item->value.integer < minimum ||
        item->value.integer > maximum) {
        return std::unexpected(settings_error(item->value.location,
                                              "integer from " + std::to_string(minimum) +
                                                  " through " + std::to_string(maximum),
                                              describe(item->value),
                                              "choose a value inside the supported range"));
    }
    return static_cast<Integer>(item->value.integer);
}

auto validate_fields(const Record& record, std::span<const std::string_view> allowed)
    -> liquid::result<void> {
    for (const auto& item : record.fields) {
        if (std::ranges::find(allowed, item.name) == allowed.end()) {
            return std::unexpected(settings_error(item.location,
                                                  "known " + record.kind + " setting",
                                                  "'" + item.name + "'",
                                                  "remove the unknown setting"));
        }
    }
    return {};
}

auto apply_theme(const Record& group, Settings& output) -> liquid::result<void> {
    constexpr auto allowed = std::array<std::string_view, 1>{"mode"};
    if (auto checked = validate_fields(group, allowed); !checked) {
        return checked;
    }
    auto mode = identifier_field(group, "mode");
    if (!mode || !*mode) {
        return mode ? liquid::result<void>{} : std::unexpected(mode.error());
    }
    if (**mode == "system") {
        output.theme = ThemePreference::system;
    } else if (**mode == "light") {
        output.theme = ThemePreference::light;
    } else if (**mode == "dark") {
        output.theme = ThemePreference::dark;
    } else {
        const auto* item = field(group, "mode");
        return std::unexpected(settings_error(item->value.location,
                                              "system, light, or dark",
                                              describe(item->value),
                                              "theme modes are system, light, and dark"));
    }
    return {};
}

auto apply_fonts(const Record& group, Settings& output) -> liquid::result<void> {
    constexpr auto allowed =
        std::array<std::string_view, 4>{"ui_family", "editor_family", "ui_size", "editor_size"};
    if (auto checked = validate_fields(group, allowed); !checked) {
        return checked;
    }
    auto ui_family = string_field(group, "ui_family");
    auto editor_family = string_field(group, "editor_family");
    auto ui_size = integer_field<std::uint32_t>(group, "ui_size", 11, 32);
    auto editor_size = integer_field<std::uint32_t>(group, "editor_size", 12, 16);
    if (!ui_family || !editor_family || !ui_size || !editor_size) {
        return std::unexpected(!ui_family       ? ui_family.error()
                               : !editor_family ? editor_family.error()
                               : !ui_size       ? ui_size.error()
                                                : editor_size.error());
    }
    if (*ui_family) {
        output.fonts.ui_family = **ui_family;
    }
    if (*editor_family) {
        output.fonts.editor_family = **editor_family;
    }
    if (*ui_size) {
        output.fonts.ui_size = **ui_size;
    }
    if (*editor_size) {
        output.fonts.editor_size = **editor_size;
    }
    return {};
}

auto apply_editor(const Record& group, Settings& output) -> liquid::result<void> {
    constexpr auto allowed =
        std::array<std::string_view, 3>{"wrap", "whitespace", "format_on_save"};
    if (auto checked = validate_fields(group, allowed); !checked) {
        return checked;
    }
    auto wrap = boolean_field(group, "wrap");
    auto whitespace = boolean_field(group, "whitespace");
    auto format = boolean_field(group, "format_on_save");
    if (!wrap || !whitespace || !format) {
        return std::unexpected(!wrap         ? wrap.error()
                               : !whitespace ? whitespace.error()
                                             : format.error());
    }
    if (*wrap) {
        output.editor.wrap = **wrap;
    }
    if (*whitespace) {
        output.editor.whitespace = **whitespace;
    }
    if (*format) {
        output.editor.format_on_save = **format;
    }
    return {};
}

auto apply_keymap(const Record& group, Settings& output) -> liquid::result<void> {
    constexpr auto allowed = std::array<std::string_view, 1>{"bindings"};
    if (auto checked = validate_fields(group, allowed); !checked) {
        return checked;
    }
    const auto* bindings = field(group, "bindings");
    if (bindings == nullptr) {
        return {};
    }
    if (bindings->value.kind != ValueKind::map) {
        return std::unexpected(settings_error(bindings->value.location,
                                              "map of command names to key bindings",
                                              describe(bindings->value),
                                              "bindings use { \"command\": \"Key\" }"));
    }
    for (const auto& [command, value] : bindings->value.map) {
        if (value.kind != ValueKind::string || value.text.empty()) {
            return std::unexpected(settings_error(value.location,
                                                  "non-empty key binding string",
                                                  describe(value),
                                                  "bindings are quoted, for example \"F7\""));
        }
        output.keymap.insert_or_assign(command, value.text);
    }
    return {};
}

auto apply_ai(const Record& group, Settings& output) -> liquid::result<void> {
    constexpr auto allowed = std::array<std::string_view, 5>{
        "enabled", "provider", "model", "context_bytes", "max_tokens"};
    if (auto checked = validate_fields(group, allowed); !checked) {
        return checked;
    }
    auto enabled = boolean_field(group, "enabled");
    auto provider = identifier_field(group, "provider");
    auto model = string_field(group, "model");
    auto context = integer_field<std::size_t>(group, "context_bytes", 512, 16 * 1024 * 1024);
    auto tokens = integer_field<std::uint32_t>(group, "max_tokens", 1, 1'000'000);
    if (!enabled || !provider || !model || !context || !tokens) {
        return std::unexpected(!enabled    ? enabled.error()
                               : !provider ? provider.error()
                               : !model    ? model.error()
                               : !context  ? context.error()
                                           : tokens.error());
    }
    if (*provider && **provider != "anthropic") {
        const auto* item = field(group, "provider");
        return std::unexpected(settings_error(item->value.location,
                                              "anthropic",
                                              describe(item->value),
                                              "anthropic is the configured provider transport"));
    }
    if (*enabled) {
        output.ai.enabled = **enabled;
    }
    if (*provider) {
        output.ai.provider = **provider;
    }
    if (*model) {
        if ((*model)->empty()) {
            const auto* item = field(group, "model");
            return std::unexpected(settings_error(item->value.location,
                                                  "non-empty model name",
                                                  "empty string",
                                                  "name the provider model"));
        }
        output.ai.model = **model;
    }
    if (*context) {
        output.ai.context_bytes = **context;
    }
    if (*tokens) {
        output.ai.max_tokens = **tokens;
    }
    return {};
}

auto apply_lake(const Record& group, Settings& output) -> liquid::result<void> {
    constexpr auto no_fields = std::array<std::string_view, 0>{};
    if (auto checked = validate_fields(group, no_fields); !checked) {
        return checked;
    }
    for (const auto& account : group.records) {
        if (account.kind != "account" || !account.name || account.name->empty()) {
            return std::unexpected(settings_error(account.location,
                                                  "named lake account",
                                                  account.kind,
                                                  "use account \"name\" { url: \"https://...\" }"));
        }
        constexpr auto allowed = std::array<std::string_view, 1>{"url"};
        if (auto checked = validate_fields(account, allowed); !checked) {
            return checked;
        }
        auto url = string_field(account, "url");
        if (!url) {
            return std::unexpected(url.error());
        }
        if (!*url || (*url)->empty()) {
            return std::unexpected(settings_error(account.location,
                                                  "lake account URL",
                                                  "missing url",
                                                  "accounts require url: \"https://...\""));
        }
        const auto duplicate =
            std::ranges::find(output.lake_accounts, *account.name, &LakeAccountSettings::name);
        if (duplicate != output.lake_accounts.end()) {
            *duplicate = {*account.name, **url};
        } else {
            output.lake_accounts.push_back({*account.name, **url});
        }
    }
    return {};
}

auto apply_telemetry(const Record& group, Settings& output) -> liquid::result<void> {
    constexpr auto allowed = std::array<std::string_view, 1>{"enabled"};
    if (auto checked = validate_fields(group, allowed); !checked) {
        return checked;
    }
    auto enabled = boolean_field(group, "enabled");
    if (!enabled) {
        return std::unexpected(enabled.error());
    }
    if (*enabled) {
        output.telemetry = **enabled;
    }
    return {};
}

auto apply_group(const Record& group, Settings& output) -> liquid::result<void> {
    if (group.kind == "theme") {
        return apply_theme(group, output);
    }
    if (group.kind == "fonts") {
        return apply_fonts(group, output);
    }
    if (group.kind == "editor") {
        return apply_editor(group, output);
    }
    if (group.kind == "keymap") {
        return apply_keymap(group, output);
    }
    if (group.kind == "ai") {
        return apply_ai(group, output);
    }
    if (group.kind == "lake") {
        return apply_lake(group, output);
    }
    if (group.kind == "telemetry") {
        return apply_telemetry(group, output);
    }
    return std::unexpected(
        settings_error(group.location,
                       "settings group",
                       "'" + group.kind + "'",
                       "use theme, fonts, editor, keymap, ai, lake, or telemetry"));
}

auto read_file(const std::filesystem::path& path) -> liquid::result<std::string> {
    auto input = std::ifstream(path, std::ios::binary);
    if (!input) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot read settings file", path.generic_string()});
    }
    return std::string(std::istreambuf_iterator<char>(input), {});
}

auto diagnostic(const std::filesystem::path& path, const liquid::error& error)
    -> SettingsDiagnostic {
    auto output = SettingsDiagnostic{.path = path, .message = error.message};
    const auto prefix = path.generic_string() + ":";
    if (!error.message.starts_with(prefix)) {
        return output;
    }
    const auto line_end = error.message.find(':', prefix.size());
    const auto column_end =
        line_end == std::string::npos ? std::string::npos : error.message.find(':', line_end + 1);
    if (line_end == std::string::npos || column_end == std::string::npos) {
        return output;
    }
    try {
        output.line = std::stoull(error.message.substr(prefix.size(), line_end - prefix.size()));
        output.column = std::stoull(error.message.substr(line_end + 1, column_end - line_end - 1));
    } catch (...) {
    }
    return output;
}

auto escape(std::string_view text) -> std::string {
    auto output = std::string{};
    for (const auto value : text) {
        switch (value) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output += value;
            break;
        }
    }
    return output;
}

auto theme_name(ThemePreference theme) -> std::string_view {
    switch (theme) {
    case ThemePreference::system:
        return "system";
    case ThemePreference::light:
        return "light";
    case ThemePreference::dark:
        return "dark";
    }
    return "system";
}

auto serialize_settings(const Settings& settings) -> std::string {
    auto output = std::ostringstream{};
    output << "liquify settings {\n";
    output << "  theme { mode: " << theme_name(settings.theme) << " }\n";
    output << "  fonts {\n";
    output << "    ui_family: \"" << escape(settings.fonts.ui_family) << "\"\n";
    output << "    editor_family: \"" << escape(settings.fonts.editor_family) << "\"\n";
    output << "    ui_size: " << settings.fonts.ui_size << "\n";
    output << "    editor_size: " << settings.fonts.editor_size << "\n";
    output << "  }\n";
    output << "  editor {\n";
    output << "    wrap: " << (settings.editor.wrap ? "true" : "false") << "\n";
    output << "    whitespace: " << (settings.editor.whitespace ? "true" : "false") << "\n";
    output << "    format_on_save: " << (settings.editor.format_on_save ? "true" : "false") << "\n";
    output << "  }\n";
    output << "  keymap { bindings: {";
    auto first = true;
    for (const auto& [command, binding] : settings.keymap) {
        output << (first ? "" : ",") << "\n      \"" << escape(command) << "\": \""
               << escape(binding) << "\"";
        first = false;
    }
    output << (settings.keymap.empty() ? "}" : "\n    }") << " }\n";
    output << "  ai {\n";
    output << "    enabled: " << (settings.ai.enabled ? "true" : "false") << "\n";
    output << "    provider: " << settings.ai.provider << "\n";
    output << "    model: \"" << escape(settings.ai.model) << "\"\n";
    output << "    context_bytes: " << settings.ai.context_bytes << "\n";
    output << "    max_tokens: " << settings.ai.max_tokens << "\n";
    output << "  }\n";
    output << "  lake {\n";
    for (const auto& account : settings.lake_accounts) {
        output << "    account \"" << escape(account.name) << "\" { url: \"" << escape(account.url)
               << "\" }\n";
    }
    output << "  }\n";
    output << "  telemetry { enabled: " << (settings.telemetry ? "true" : "false") << " }\n";
    output << "}\n";
    return output.str();
}

auto lower(std::string_view text) -> std::string {
    auto output = std::string{text};
    std::ranges::transform(output, output.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return output;
}

} // namespace

auto user_settings_path() -> std::filesystem::path {
#ifdef _WIN32
    if (const auto* app_data = std::getenv("APPDATA")) {
        return std::filesystem::path(app_data) / "liquify" / "settings.liquid";
    }
#elif defined(__APPLE__)
    if (const auto* user_home = std::getenv("HOME")) {
        return std::filesystem::path(user_home) / "Library" / "Application Support" / "liquify" /
               "settings.liquid";
    }
#else
    if (const auto* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg_config) / "liquify" / "settings.liquid";
    }
    if (const auto* user_home = std::getenv("HOME")) {
        return std::filesystem::path(user_home) / ".config" / "liquify" / "settings.liquid";
    }
#endif
    return {};
}

auto repository_settings_path(const std::filesystem::path& repository) -> std::filesystem::path {
    return repository / ".liquid" / "settings.liquid";
}

auto parse_settings(std::string_view text, const std::filesystem::path& path, Settings base)
    -> liquid::result<Settings> {
    auto parsed = liquid::parse_configuration(text, path);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    for (const auto& record : parsed->records) {
        if (record.kind != "liquify" || record.name != "settings") {
            return std::unexpected(settings_error(record.location,
                                                  "liquify settings record",
                                                  "'" + record.kind + "'",
                                                  "use liquify settings { ... }"));
        }
        if (!record.fields.empty()) {
            return std::unexpected(settings_error(record.fields.front().location,
                                                  "settings group",
                                                  "field '" + record.fields.front().name + "'",
                                                  "place values inside a named settings group"));
        }
        for (const auto& group : record.records) {
            auto applied = apply_group(group, base);
            if (!applied) {
                return std::unexpected(applied.error());
            }
        }
    }
    return base;
}

auto load_settings(const std::optional<std::filesystem::path>& repository,
                   std::optional<std::filesystem::path> user_path) -> SettingsLoad {
    auto output = SettingsLoad{};
    const auto apply_file = [&output](const std::filesystem::path& path) {
        if (path.empty() || !std::filesystem::exists(path)) {
            return;
        }
        auto text = read_file(path);
        if (!text) {
            output.diagnostics.push_back(diagnostic(path, text.error()));
            return;
        }
        auto parsed = parse_settings(*text, path, output.settings);
        if (!parsed) {
            output.diagnostics.push_back(diagnostic(path, parsed.error()));
            return;
        }
        output.settings = std::move(*parsed);
    };
    apply_file(user_path.value_or(user_settings_path()));
    if (repository) {
        apply_file(repository_settings_path(*repository));
    }
    return output;
}

auto save_settings(const std::filesystem::path& path, const Settings& settings)
    -> liquid::result<void> {
    const auto serialized = serialize_settings(settings);
    auto validated = parse_settings(serialized, path);
    if (!validated) {
        return std::unexpected(validated.error());
    }
    auto code = std::error_code{};
    std::filesystem::create_directories(path.parent_path(), code);
    if (code) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot create settings directory", code.message()});
    }
    auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot write settings file", path.generic_string()});
    }
    output << serialized;
    if (!output) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot write settings file", path.generic_string()});
    }
    return {};
}

void reset_settings_group(Settings& settings, SettingsGroup group) {
    const auto defaults = Settings{};
    switch (group) {
    case SettingsGroup::appearance:
        settings.theme = defaults.theme;
        settings.fonts = defaults.fonts;
        break;
    case SettingsGroup::editor:
        settings.editor = defaults.editor;
        break;
    case SettingsGroup::keymap:
        settings.keymap = defaults.keymap;
        break;
    case SettingsGroup::ai:
        settings.ai = defaults.ai;
        break;
    case SettingsGroup::lake:
        settings.lake_accounts = defaults.lake_accounts;
        break;
    case SettingsGroup::telemetry:
        settings.telemetry = defaults.telemetry;
        break;
    }
}

auto settings_group_matches(SettingsGroup group, std::string_view query) -> bool {
    if (query.empty()) {
        return true;
    }
    const auto needle = lower(query);
    auto terms = std::string{settings_group_name(group)};
    switch (group) {
    case SettingsGroup::appearance:
        terms += " theme light dark system font size IBM Plex";
        break;
    case SettingsGroup::editor:
        terms += " wrap whitespace format save editor behaviour";
        break;
    case SettingsGroup::keymap:
        terms += " keyboard shortcut command palette binding";
        break;
    case SettingsGroup::ai:
        terms += " assistant provider anthropic model token key";
        break;
    case SettingsGroup::lake:
        terms += " account url token credential";
        break;
    case SettingsGroup::telemetry:
        terms += " analytics privacy off";
        break;
    }
    return lower(terms).contains(needle);
}

auto settings_group_name(SettingsGroup group) -> std::string_view {
    switch (group) {
    case SettingsGroup::appearance:
        return "Appearance";
    case SettingsGroup::editor:
        return "Editor";
    case SettingsGroup::keymap:
        return "Keymap";
    case SettingsGroup::ai:
        return "AI";
    case SettingsGroup::lake:
        return "Lake accounts";
    case SettingsGroup::telemetry:
        return "Telemetry";
    }
    return "Settings";
}

} // namespace liquify
