// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "liquid/error.hpp"

namespace liquify {

struct AiConfig {
    bool enabled{};
    std::size_t context_bytes{64 * 1024};
    std::string model{"claude-opus-5"};
    std::uint32_t max_tokens{4'096};
};

struct ConfiguredSecret {
    bool configured{};
    std::string value;
};

[[nodiscard]] auto global_ai_config_path() -> std::filesystem::path;
// AI is turned on per user and per repository, never by a settings file (liquify#75): the
// repositories a user enabled it for are listed, one canonical path per line, in
// `ai-repositories` beside the user's settings file (or beside `global_path` when given).
[[nodiscard]] auto ai_repositories_path(std::optional<std::filesystem::path> global_path =
                                            std::nullopt) -> std::filesystem::path;
[[nodiscard]] auto repository_ai_enabled(const std::filesystem::path& repository_path,
                                         std::optional<std::filesystem::path> global_path =
                                             std::nullopt) -> bool;
[[nodiscard]] auto load_ai_config(const std::filesystem::path& repository_path,
                                  std::optional<std::filesystem::path> global_path = std::nullopt)
    -> liquid::result<AiConfig>;
[[nodiscard]] auto set_repository_ai_enabled(const std::filesystem::path& repository_path,
                                             bool enabled,
                                             std::optional<std::filesystem::path> global_path =
                                                 std::nullopt) -> liquid::result<void>;
[[nodiscard]] auto anthropic_api_key() -> ConfiguredSecret;
[[nodiscard]] auto lake_account_token(std::string_view account) -> ConfiguredSecret;
[[nodiscard]] auto store_anthropic_api_key(std::string_view value) -> liquid::result<void>;
[[nodiscard]] auto store_lake_account_token(std::string_view account, std::string_view value)
    -> liquid::result<void>;

} // namespace liquify
