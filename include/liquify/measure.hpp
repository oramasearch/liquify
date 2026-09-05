#pragma once

#include <filesystem>
#include <string>

namespace liquify {

[[nodiscard]] auto measure_build(const std::filesystem::path& repository, std::string target)
    -> int;

} // namespace liquify
