// SPDX-License-Identifier: MIT

#include "liquify/version.hpp"

#include <format>

#include "liquid/version.hpp"

namespace liquify {
std::string_view version_string() noexcept {
    return "0.0.1+" LIQUIFY_REVISION;
}

std::string_view liquid_revision() noexcept {
    return LIQUIFY_LIQUID_REVISION;
}

std::string build_description() {
    return std::format("liquify {} (libliquid {}+{}; store 1; object maps 1-5)",
                       version_string(),
                       liquid::version_string(),
                       liquid_revision());
}
} // namespace liquify
