// SPDX-License-Identifier: MIT

#include "liquify/ai_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "liquify/settings.hpp"

#ifdef __APPLE__
#include <Security/Security.h>
#elif defined(_WIN32)
#include <wincred.h>
#include <windows.h>
#endif

namespace liquify {
namespace {

#ifdef __APPLE__
auto keychain_secret(std::string_view account) -> std::string {
    void* data = nullptr;
    UInt32 size = 0;
    const auto status = SecKeychainFindGenericPassword(nullptr,
                                                       7,
                                                       "liquify",
                                                       static_cast<UInt32>(account.size()),
                                                       account.data(),
                                                       &size,
                                                       &data,
                                                       nullptr);
    if (status != errSecSuccess || data == nullptr) {
        return {};
    }
    auto output = std::string(static_cast<const char*>(data), size);
    SecKeychainItemFreeContent(nullptr, data);
    return output;
}

auto store_keychain_secret(std::string_view account, std::string_view value)
    -> liquid::result<void> {
    SecKeychainItemRef item = nullptr;
    const auto found = SecKeychainFindGenericPassword(nullptr,
                                                      7,
                                                      "liquify",
                                                      static_cast<UInt32>(account.size()),
                                                      account.data(),
                                                      nullptr,
                                                      nullptr,
                                                      &item);
    OSStatus status = found;
    if (found == errSecSuccess) {
        status = SecKeychainItemModifyAttributesAndData(
            item, nullptr, static_cast<UInt32>(value.size()), value.data());
        CFRelease(item);
    } else if (found == errSecItemNotFound) {
        status = SecKeychainAddGenericPassword(nullptr,
                                               7,
                                               "liquify",
                                               static_cast<UInt32>(account.size()),
                                               account.data(),
                                               static_cast<UInt32>(value.size()),
                                               value.data(),
                                               nullptr);
    }
    if (status != errSecSuccess) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot store OS credential", std::to_string(status)});
    }
    return {};
}
#elif defined(_WIN32)
auto widen(std::string_view text) -> std::wstring {
    return {text.begin(), text.end()};
}

auto keychain_secret(std::string_view account) -> std::string {
    auto target = widen("liquify/" + std::string(account));
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        return {};
    }
    auto output = std::string(reinterpret_cast<const char*>(credential->CredentialBlob),
                              credential->CredentialBlobSize);
    CredFree(credential);
    return output;
}

auto store_keychain_secret(std::string_view account, std::string_view value)
    -> liquid::result<void> {
    auto target = widen("liquify/" + std::string(account));
    auto credential = CREDENTIALW{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(value.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&credential, 0)) {
        return std::unexpected(liquid::error{
            liquid::errc::io, "cannot store OS credential", std::to_string(GetLastError())});
    }
    return {};
}
#else
auto keychain_secret(std::string_view) -> std::string {
    return {};
}

auto store_keychain_secret(std::string_view, std::string_view) -> liquid::result<void> {
    return std::unexpected(liquid::error{liquid::errc::unavailable,
                                         "OS credential store is unavailable",
                                         "set the credential through the process environment"});
}
#endif

auto configured_secret(std::string_view account, const char* environment_name) -> ConfiguredSecret {
    auto value = keychain_secret(account);
    if (value.empty()) {
        if (const auto* environment = std::getenv(environment_name)) {
            value = environment;
        }
    }
    return {!value.empty(), std::move(value)};
}

auto settings_failure(const SettingsDiagnostic& diagnostic) -> liquid::error {
    return {liquid::errc::invalid_argument, diagnostic.message, diagnostic.path.generic_string()};
}

} // namespace

auto global_ai_config_path() -> std::filesystem::path {
    return user_settings_path();
}

namespace {

auto repository_identity(const std::filesystem::path& repository_path) -> std::string {
    auto code = std::error_code{};
    auto canonical = std::filesystem::weakly_canonical(repository_path, code);
    return (code ? std::filesystem::absolute(repository_path) : canonical).generic_string();
}

auto enabled_repositories(const std::filesystem::path& list) -> std::vector<std::string> {
    auto input = std::ifstream(list);
    auto lines = std::vector<std::string>{};
    for (auto line = std::string{}; std::getline(input, line);) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

} // namespace

auto ai_repositories_path(std::optional<std::filesystem::path> global_path)
    -> std::filesystem::path {
    const auto settings = global_path.value_or(user_settings_path());
    return settings.empty() ? std::filesystem::path{} : settings.parent_path() / "ai-repositories";
}

auto repository_ai_enabled(const std::filesystem::path& repository_path,
                           std::optional<std::filesystem::path> global_path) -> bool {
    const auto list = ai_repositories_path(global_path);
    if (list.empty()) {
        return false;
    }
    const auto identity = repository_identity(repository_path);
    return std::ranges::contains(enabled_repositories(list), identity);
}

auto load_ai_config(const std::filesystem::path& repository_path,
                    std::optional<std::filesystem::path> global_path) -> liquid::result<AiConfig> {
    auto loaded = load_settings(repository_path, global_path);
    if (!loaded.diagnostics.empty()) {
        return std::unexpected(settings_failure(loaded.diagnostics.front()));
    }
    return AiConfig{.enabled = repository_ai_enabled(repository_path, global_path),
                    .context_bytes = loaded.settings.ai.context_bytes,
                    .model = loaded.settings.ai.model,
                    .max_tokens = loaded.settings.ai.max_tokens};
}

auto set_repository_ai_enabled(const std::filesystem::path& repository_path,
                               bool enabled,
                               std::optional<std::filesystem::path> global_path)
    -> liquid::result<void> {
    const auto list = ai_repositories_path(global_path);
    if (list.empty()) {
        return std::unexpected(liquid::error{liquid::errc::unavailable,
                                             "no user settings directory",
                                             "set HOME, XDG_CONFIG_HOME or APPDATA"});
    }
    const auto identity = repository_identity(repository_path);
    auto lines = enabled_repositories(list);
    std::erase(lines, identity);
    if (enabled) {
        lines.push_back(identity);
    }
    auto code = std::error_code{};
    std::filesystem::create_directories(list.parent_path(), code);
    if (code) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot create settings directory", code.message()});
    }
    auto output = std::ofstream(list, std::ios::binary | std::ios::trunc);
    if (!output) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot write the AI opt-in list", list.generic_string()});
    }
    for (const auto& line : lines) {
        output << line << "\n";
    }
    if (!output) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot write the AI opt-in list", list.generic_string()});
    }
    return {};
}

auto anthropic_api_key() -> ConfiguredSecret {
    return configured_secret("ANTHROPIC_API_KEY", "ANTHROPIC_API_KEY");
}

auto lake_account_token(std::string_view account) -> ConfiguredSecret {
    return configured_secret("LAKE_TOKEN/" + std::string(account), "LIQUID_TOKEN");
}

auto store_anthropic_api_key(std::string_view value) -> liquid::result<void> {
    return store_keychain_secret("ANTHROPIC_API_KEY", value);
}

auto store_lake_account_token(std::string_view account, std::string_view value)
    -> liquid::result<void> {
    return store_keychain_secret("LAKE_TOKEN/" + std::string(account), value);
}

} // namespace liquify
