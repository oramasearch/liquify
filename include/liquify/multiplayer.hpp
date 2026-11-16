// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "liquid/error.hpp"
#include "liquid/session.hpp"

namespace liquify {

struct MultiplayerSnapshot {
    liquid::session_id session;
    std::vector<liquid::presence> presence;
    std::vector<liquid::semantic_lease> leases;
    std::vector<liquid::line_attribution> attribution;
    liquid::change_id change;
};

[[nodiscard]] auto make_multiplayer_session() -> liquid::session_id;
[[nodiscard]] auto refresh_multiplayer(const std::filesystem::path& repository_path,
                                       std::string path,
                                       liquid::session_id session,
                                       liquid::frontend_source_range range)
    -> liquid::result<MultiplayerSnapshot>;
[[nodiscard]] auto acquire_multiplayer_lease(const std::filesystem::path& repository_path,
                                             liquid::session_id session,
                                             liquid::lease_claim claim) -> liquid::result<void>;
[[nodiscard]] auto release_multiplayer_lease(const std::filesystem::path& repository_path,
                                             liquid::session_id session,
                                             liquid::lease_claim claim) -> liquid::result<void>;
[[nodiscard]] auto multiplayer_presence_text(const liquid::presence& value) -> std::string;
[[nodiscard]] auto multiplayer_lease_text(const liquid::semantic_lease& value) -> std::string;
[[nodiscard]] auto multiplayer_attribution_text(const liquid::line_attribution& value)
    -> std::string;

} // namespace liquify
