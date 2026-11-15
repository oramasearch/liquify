#include "liquify/build_setup.hpp"

#include <algorithm>
#include <format>

#include "liquid/build.hpp"
#include "liquid/runtime.hpp"

namespace liquify {
namespace {

auto has(std::span<const std::string> names, std::string_view name) -> bool {
    return std::ranges::find(names, name) != names.end();
}

} // namespace

auto propose_build_command(std::span<const std::string> root_names) -> BuildProposal {
    if (has(root_names, "CMakeLists.txt")) {
        return {"cmake -S . -B build && cmake --build build",
                "This repository has a CMakeLists.txt, so liquify suggests CMake."};
    }
    if (has(root_names, "Makefile") || has(root_names, "makefile") ||
        has(root_names, "GNUmakefile")) {
        return {"make", "This repository has a Makefile, so liquify suggests make."};
    }
    if (has(root_names, "build.sh")) {
        return {"sh ./build.sh", "This repository has a build.sh script."};
    }
    return {{}, "liquify could not tell how this repository builds. Type the command you run."};
}

auto record_build_command(const std::filesystem::path& repository, const std::string& command)
    -> UserError {
    if (command.find_first_not_of(" \t") == std::string::npos) {
        return {.kind = FailureKind::local,
                .summary = "A build needs a command",
                .next_step = "Type the command you run to build this repository."};
    }
    auto report = liquid::runtime::block_on(
        liquid::build::wrap(repository, {"/bin/sh", "-c", command}));
    if (!report) {
        return {.kind = FailureKind::local,
                .summary = "The build command could not be recorded",
                .next_step = "Check the command, then try again.",
                .details = error_details(report.error(), repository)};
    }
    if (report->exit != 0) {
        return {.kind = FailureKind::local,
                .summary = std::format("The build command failed (exit {})", report->exit),
                .next_step = "liquid recorded nothing. Fix the command or the code, then try "
                             "again.",
                .details = command};
    }
    return {};
}

} // namespace liquify
