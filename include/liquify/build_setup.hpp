// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <span>
#include <string>

#include "liquify/user_error.hpp"

namespace liquify {

// The build command liquify suggests for a repository with no recorded build, and why.
struct BuildProposal {
    std::string command; // a shell command line; empty when liquify cannot tell
    std::string reason;  // one sentence naming what the suggestion is based on
};

// Suggests a build command from the names at the repository's root: CMake, then Make, then a
// build script. The person can change it before it is recorded.
[[nodiscard]] auto propose_build_command(std::span<const std::string> root_names)
    -> BuildProposal;

// Runs `command` once through libliquid's wrap API (liquid::build::wrap, `/bin/sh -c command`)
// in the repository, which records it as the repository's build: the same record
// `liquid wrap <command>` makes. Empty on success.
[[nodiscard]] auto record_build_command(const std::filesystem::path& repository,
                                        const std::string& command) -> UserError;

} // namespace liquify
