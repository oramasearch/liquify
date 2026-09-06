#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

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
[[nodiscard]] auto load_ai_config(const std::filesystem::path& repository_path,
                                  std::optional<std::filesystem::path> global_path = std::nullopt)
    -> liquid::result<AiConfig>;
[[nodiscard]] auto set_repository_ai_enabled(const std::filesystem::path& repository_path,
                                             bool enabled) -> liquid::result<void>;
[[nodiscard]] auto anthropic_api_key() -> ConfiguredSecret;

} // namespace liquify
