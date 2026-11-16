// SPDX-License-Identifier: MIT

#pragma once
#include <string>
#include <string_view>

namespace liquify {

struct version {
    unsigned major;
    unsigned minor;
    unsigned patch;
};

[[nodiscard]] constexpr version current_version() noexcept {
    return {LIQUIFY_VERSION_MAJOR, LIQUIFY_VERSION_MINOR, LIQUIFY_VERSION_PATCH};
}
[[nodiscard]] std::string_view version_string() noexcept;
[[nodiscard]] std::string_view liquid_revision() noexcept;
[[nodiscard]] std::string build_description();

} // namespace liquify
