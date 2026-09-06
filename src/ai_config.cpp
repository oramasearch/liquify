#include "liquify/ai_config.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <map>
#include <span>
#include <string_view>
#include <vector>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

#ifdef __APPLE__
#include <Security/Security.h>
#elif defined(_WIN32)
#include <wincred.h>
#include <windows.h>
#endif

namespace liquify {
namespace {

constexpr auto enabled_key = "config.liquify.ai.enabled";
constexpr auto context_bytes_key = "config.liquify.ai.context_bytes";
constexpr auto model_key = "config.liquify.ai.model";
constexpr auto max_tokens_key = "config.liquify.ai.max_tokens";

auto config_error(std::string message, std::string context) -> liquid::error {
    return {liquid::errc::invalid_argument, std::move(message), std::move(context)};
}

auto trim(std::string_view value) -> std::string_view {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

auto parse_file(const std::filesystem::path& path)
    -> liquid::result<std::map<std::string, std::string>> {
    auto values = std::map<std::string, std::string>{};
    if (path.empty() || !std::filesystem::exists(path)) {
        return values;
    }
    auto input = std::ifstream(path);
    if (!input) {
        return std::unexpected(config_error("could not open global configuration", path.string()));
    }
    auto line = std::string{};
    auto line_number = std::size_t{};
    while (std::getline(input, line)) {
        ++line_number;
        auto view = trim(line);
        if (view.empty() || view.starts_with('#')) {
            continue;
        }
        const auto equals = view.find('=');
        if (equals == std::string_view::npos) {
            return std::unexpected(config_error("expected key=value",
                                                path.string() + ":" + std::to_string(line_number)));
        }
        const auto key = std::string(trim(view.substr(0, equals)));
        const auto value = std::string(trim(view.substr(equals + 1)));
        values.insert_or_assign(key, value);
    }
    return values;
}

auto parse_bool(std::string_view value, std::string_view key) -> liquid::result<bool> {
    if (value == "true" || value == "1" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "off") {
        return false;
    }
    return std::unexpected(config_error("expected true or false", std::string(key)));
}

template <class Integer>
auto parse_integer(std::string_view value, std::string_view key) -> liquid::result<Integer> {
    auto output = Integer{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::unexpected(config_error("expected an integer", std::string(key)));
    }
    return output;
}

auto apply_value(AiConfig& config, std::string_view key, std::string_view value)
    -> liquid::result<void> {
    if (key == enabled_key) {
        auto parsed = parse_bool(value, key);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        config.enabled = *parsed;
    } else if (key == context_bytes_key) {
        auto parsed = parse_integer<std::size_t>(value, key);
        if (!parsed || *parsed < 512) {
            return std::unexpected(parsed ? config_error("must be at least 512", std::string(key))
                                          : parsed.error());
        }
        config.context_bytes = *parsed;
    } else if (key == model_key) {
        if (value.empty()) {
            return std::unexpected(config_error("model cannot be empty", std::string(key)));
        }
        config.model = value;
    } else if (key == max_tokens_key) {
        auto parsed = parse_integer<std::uint32_t>(value, key);
        if (!parsed || *parsed == 0) {
            return std::unexpected(parsed
                                       ? config_error("must be greater than zero", std::string(key))
                                       : parsed.error());
        }
        config.max_tokens = *parsed;
    }
    return {};
}

auto bytes_to_string(const std::vector<std::byte>& bytes) -> std::string {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

auto string_to_bytes(std::string_view text) -> std::vector<std::byte> {
    const auto characters = std::span(text.data(), text.size());
    const auto bytes = std::as_bytes(characters);
    return {bytes.begin(), bytes.end()};
}

auto read_repository_values(liquid::repository& repository, AiConfig& config)
    -> liquid::result<void> {
    for (const auto key : {enabled_key, context_bytes_key, model_key, max_tokens_key}) {
        auto value = liquid::runtime::block_on(repository.metadata(key));
        if (!value) {
            return std::unexpected(value.error());
        }
        if (!*value) {
            continue;
        }
        auto applied = apply_value(config, key, bytes_to_string(**value));
        if (!applied) {
            return std::unexpected(applied.error());
        }
    }
    return {};
}

#ifdef __APPLE__
auto keychain_api_key() -> std::string {
    void* data = nullptr;
    UInt32 size = 0;
    const auto status = SecKeychainFindGenericPassword(
        nullptr, 7, "liquify", 17, "ANTHROPIC_API_KEY", &size, &data, nullptr);
    if (status != errSecSuccess || data == nullptr) {
        return {};
    }
    auto output = std::string(static_cast<const char*>(data), size);
    SecKeychainItemFreeContent(nullptr, data);
    return output;
}
#elif defined(_WIN32)
auto keychain_api_key() -> std::string {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(L"liquify/ANTHROPIC_API_KEY", CRED_TYPE_GENERIC, 0, &credential)) {
        return {};
    }
    auto output = std::string(reinterpret_cast<const char*>(credential->CredentialBlob),
                              credential->CredentialBlobSize);
    CredFree(credential);
    return output;
}
#else
auto keychain_api_key() -> std::string {
    return {};
}
#endif

} // namespace

auto global_ai_config_path() -> std::filesystem::path {
#ifdef _WIN32
    if (const auto* app_data = std::getenv("APPDATA")) {
        return std::filesystem::path(app_data) / "liquify" / "config";
    }
#elif defined(__APPLE__)
    if (const auto* user_home = std::getenv("HOME")) {
        return std::filesystem::path(user_home) / "Library" / "Application Support" / "liquify" /
               "config";
    }
#else
    if (const auto* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg_config) / "liquify" / "config";
    }
    if (const auto* user_home = std::getenv("HOME")) {
        return std::filesystem::path(user_home) / ".config" / "liquify" / "config";
    }
#endif
    return {};
}

auto load_ai_config(const std::filesystem::path& repository_path,
                    std::optional<std::filesystem::path> global_path) -> liquid::result<AiConfig> {
    auto config = AiConfig{};
    auto global = parse_file(global_path.value_or(global_ai_config_path()));
    if (!global) {
        return std::unexpected(global.error());
    }
    for (const auto& [key, value] : *global) {
        auto applied = apply_value(config, key, value);
        if (!applied) {
            return std::unexpected(applied.error());
        }
    }
    auto opened = liquid::runtime::block_on(liquid::repository::open(repository_path));
    if (!opened) {
        return std::unexpected(opened.error());
    }
    auto loaded = read_repository_values(*opened, config);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    return config;
}

auto set_repository_ai_enabled(const std::filesystem::path& repository_path, bool enabled)
    -> liquid::result<void> {
    auto opened = liquid::runtime::block_on(liquid::repository::open(repository_path));
    if (!opened) {
        return std::unexpected(opened.error());
    }
    return liquid::runtime::block_on(
        opened->set_metadata(enabled_key, string_to_bytes(enabled ? "true" : "false")));
}

auto anthropic_api_key() -> ConfiguredSecret {
    auto value = keychain_api_key();
    if (value.empty()) {
        if (const auto* environment = std::getenv("ANTHROPIC_API_KEY")) {
            value = environment;
        }
    }
    return ConfiguredSecret{!value.empty(), std::move(value)};
}

} // namespace liquify
