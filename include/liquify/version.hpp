#pragma once
#include <string_view>

namespace liquify {

struct version {
    unsigned major;
    unsigned minor;
    unsigned patch;
};

[[nodiscard]] constexpr version current_version() noexcept { return {0, 0, 1}; }
[[nodiscard]] std::string_view version_string() noexcept;

}  // namespace liquify
