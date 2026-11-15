// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include "liquid/error.hpp"

namespace liquify {

// Add artifact and upgrade guidance to format failures returned by libliquid.
[[nodiscard]] std::string repository_error_message(const liquid::error& error);

} // namespace liquify
