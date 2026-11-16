// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>

#include "liquid/error.hpp"
#include "liquify/user_error.hpp"

namespace liquify {

// The lake repository a local repository pushes to and reviews on. A clone from lake records
// it (libliquid's drive client keeps the address it cloned from); Set Lake records it for a
// repository made here. Returns an empty string when none is recorded.
[[nodiscard]] auto recorded_lake_remote(const std::filesystem::path& repository)
    -> liquid::result<std::string>;

// Records `remote` as the repository's lake through libliquid's drive client. Makes no
// network call. A repository that already has a different lake is refused.
[[nodiscard]] auto record_lake_remote(const std::filesystem::path& repository,
                                      const std::string& remote) -> UserError;

// The token liquify sends to lake: LIQUID_TOKEN, as the liquid CLI reads it.
[[nodiscard]] auto lake_token() -> std::string;

} // namespace liquify
