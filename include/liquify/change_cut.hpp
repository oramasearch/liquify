// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "liquid/data.hpp"
#include "liquify/user_error.hpp"

namespace liquify {

// One saved drip as the Cut Change composer lists it.
struct PendingDrip {
    std::string id;     // short form, for display
    std::string author; // the actor's identifier
    std::chrono::sys_seconds at{};
    std::vector<std::string> paths;
};

// What a cut would gather: the change the work sits on and the drips saved since.
struct PendingChange {
    std::string base;              // the current change, short form; empty in a new repository
    std::vector<PendingDrip> drips;
    std::vector<std::string> paths; // every file the drips touch, sorted, once each
    UserError failure;
};

struct CutResult {
    std::string change; // the new change, full form
    UserError failure;
};

// Reads the pending drips through libliquid. Runs off the render thread.
[[nodiscard]] auto load_pending_change(const std::filesystem::path& repository) -> PendingChange;

// Names the saved drips as one change with `message`, through libliquid's cut API: the same
// change `liquid cut <message>` records, signed by this computer's identity.
[[nodiscard]] auto cut_change(const std::filesystem::path& repository, std::string message,
                              liquid::session_id session = {}) -> CutResult;

// The first line of a message, trimmed; a cut needs one.
[[nodiscard]] auto cut_message_summary(std::string_view message) -> std::string;

} // namespace liquify
