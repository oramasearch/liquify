// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <string>

#include "liquify/build_setup.hpp"

namespace {

TEST(BuildSetup, ProposesACommandFromWhatTheRepositoryHolds) {
    const auto cmake = std::to_array<std::string>({"README.md", "CMakeLists.txt", "Makefile"});
    EXPECT_EQ(liquify::propose_build_command(cmake).command,
              "cmake -S . -B build && cmake --build build");
    const auto make = std::to_array<std::string>({"Makefile", "src"});
    EXPECT_EQ(liquify::propose_build_command(make).command, "make");
    const auto script = std::to_array<std::string>({"build.sh"});
    EXPECT_EQ(liquify::propose_build_command(script).command, "sh ./build.sh");
    const auto nothing = std::to_array<std::string>({"notes.txt"});
    const auto unknown = liquify::propose_build_command(nothing);
    EXPECT_TRUE(unknown.command.empty());
    EXPECT_FALSE(unknown.reason.empty());
}

TEST(BuildSetup, RefusesAnEmptyCommandBeforeCallingLiquid) {
    const auto failure = liquify::record_build_command("/nonexistent", "   ");
    EXPECT_EQ(failure.summary, "A build needs a command");
}

} // namespace
