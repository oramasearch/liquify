#include "liquify/ai_config.hpp"

#include <cstdlib>
#include <fstream>
#include <string_view>

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

auto load_ai_config(const std::filesystem::path& repository_path,
                    std::optional<std::filesystem::path> global_path) -> liquid::result<AiConfig> {
    auto loaded = load_settings(repository_path, global_path);
    if (!loaded.diagnostics.empty()) {
        return std::unexpected(settings_failure(loaded.diagnostics.front()));
    }
    return AiConfig{.enabled = loaded.settings.ai.enabled,
                    .context_bytes = loaded.settings.ai.context_bytes,
                    .model = loaded.settings.ai.model,
                    .max_tokens = loaded.settings.ai.max_tokens};
}

auto set_repository_ai_enabled(const std::filesystem::path& repository_path, bool enabled)
    -> liquid::result<void> {
    const auto path = repository_settings_path(repository_path);
    auto code = std::error_code{};
    std::filesystem::create_directories(path.parent_path(), code);
    if (code) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot create settings directory", code.message()});
    }
    auto output = std::ofstream(path, std::ios::binary | std::ios::app);
    if (!output) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot write settings file", path.generic_string()});
    }
    output << "liquify settings { ai { enabled: " << (enabled ? "true" : "false") << " } }\n";
    if (!output) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "cannot write settings file", path.generic_string()});
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
