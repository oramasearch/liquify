#include <gtest/gtest.h>

#include <set>
#include <string>

#include "liquify/user_error.hpp"

namespace {

using liquify::FailureKind;

auto kind(liquid::errc code, std::string message, std::string context = "sync endpoint")
    -> FailureKind {
    return liquify::classify_lake_failure({code, std::move(message), std::move(context)});
}

TEST(UserError, ClassifiesUnreachableLakesAsOfflineOnly) {
    // The resolver and connect messages liquid's HTTP client passes through on each platform.
    EXPECT_EQ(kind(liquid::errc::unavailable, "nodename nor servname provided, or not known"),
              FailureKind::offline);
    EXPECT_EQ(kind(liquid::errc::unavailable, "Connection refused"), FailureKind::offline);
    EXPECT_EQ(kind(liquid::errc::unavailable, "Operation timed out"), FailureKind::offline);
    EXPECT_EQ(kind(liquid::errc::unavailable, "No route to host"), FailureKind::offline);
    EXPECT_EQ(kind(liquid::errc::unavailable, "Name or service not known"), FailureKind::offline);
    // The TLS handshake failure of liquid#368: the lake answered.
    EXPECT_EQ(kind(liquid::errc::unavailable, "unspecified system_category error"),
              FailureKind::secure_connection);
    EXPECT_EQ(kind(liquid::errc::io, "certificate verify failed"), FailureKind::secure_connection);
}

TEST(UserError, ClassifiesAnswersFromTheLakeByLiquidsCode) {
    EXPECT_EQ(kind(liquid::errc::unauthorized, "sync endpoint returned HTTP 401"),
              FailureKind::unauthorized);
    EXPECT_EQ(kind(liquid::errc::unauthorized, "token has no permission on this repository"),
              FailureKind::forbidden);
    EXPECT_EQ(kind(liquid::errc::not_found, "sync endpoint returned HTTP 404"),
              FailureKind::not_found);
    EXPECT_EQ(kind(liquid::errc::unavailable, "sync endpoint returned HTTP 503"),
              FailureKind::lake_unavailable);
    EXPECT_EQ(kind(liquid::errc::busy, "sync endpoint returned HTTP 429"),
              FailureKind::lake_unavailable);
    EXPECT_EQ(kind(liquid::errc::unsupported, "unknown sync protocol version"),
              FailureKind::unsupported_lake);
    EXPECT_EQ(kind(liquid::errc::invalid_argument, "sync endpoint returned HTTP 400"),
              FailureKind::unsupported_lake);
    EXPECT_EQ(kind(liquid::errc::corrupt, "push reply has an invalid reference outcome"),
              FailureKind::unsupported_lake);
    EXPECT_EQ(kind(liquid::errc::io, "short HTTP write"), FailureKind::interrupted);
}

TEST(UserError, SaysOfflineIsNormalOnlyWhenTheLakeWasNotReached) {
    const auto remote = std::string{"https://lake-dev.example:8443/v1/repositories/abc"};
    for (const auto code : {liquid::errc::unauthorized, liquid::errc::not_found,
                            liquid::errc::unsupported, liquid::errc::busy, liquid::errc::io,
                            liquid::errc::internal}) {
        const auto failure =
            liquify::describe_clone_failure({code, "unspecified system_category error", ""}, remote);
        EXPECT_FALSE(failure.offline()) << liquify::failure_kind_name(failure.kind);
        EXPECT_EQ(failure.next_step.find("offline"), std::string::npos);
        EXPECT_NE(failure.summary.find("lake-dev.example"), std::string::npos);
        EXPECT_FALSE(failure.next_step.empty());
    }
    const auto unreachable = liquify::describe_clone_failure(
        {liquid::errc::unavailable, "Connection refused", "sync endpoint"}, remote);
    EXPECT_TRUE(unreachable.offline());
    EXPECT_EQ(unreachable.summary, "Could not reach lake-dev.example");
    EXPECT_NE(unreachable.next_step.find("offline is normal"), std::string::npos);
    EXPECT_EQ(unreachable.details, "sync endpoint: Connection refused");
}

TEST(UserError, EachFailureClassHasItsOwnSentenceAndStep) {
    const auto remote = std::string{"https://lake.example/v1/repositories/abc"};
    auto summaries = std::set<std::string>{};
    auto steps = std::set<std::string>{};
    for (const auto& error : {liquid::error{liquid::errc::unavailable, "Connection refused", ""},
                              liquid::error{liquid::errc::unavailable, "handshake failed", ""},
                              liquid::error{liquid::errc::unauthorized, "HTTP 401", ""},
                              liquid::error{liquid::errc::unauthorized, "HTTP 403 forbidden", ""},
                              liquid::error{liquid::errc::not_found, "HTTP 404", ""},
                              liquid::error{liquid::errc::busy, "HTTP 429", ""},
                              liquid::error{liquid::errc::unsupported, "unknown", ""}}) {
        const auto failure = liquify::describe_clone_failure(error, remote);
        summaries.insert(failure.summary);
        steps.insert(failure.next_step);
    }
    EXPECT_EQ(summaries.size(), 7U);
    EXPECT_EQ(steps.size(), 7U);
}

TEST(UserError, NamesTheLakeByItsHost) {
    EXPECT_EQ(liquify::lake_host("https://lake.example/v1/repositories/x"), "lake.example");
    EXPECT_EQ(liquify::lake_host("https://token@lake.example:8443/r"), "lake.example");
    EXPECT_EQ(liquify::lake_host("http://[::1]:9/r"), "[::1]:9");
    EXPECT_EQ(liquify::lake_host("lake.example"), "lake.example");
}

TEST(UserError, RemovesAbsolutePathsFromDetails) {
    const auto repository = std::filesystem::path{"/Users/ora/projects/sample"};
    EXPECT_EQ(liquify::without_absolute_paths(
                  "/Users/ora/projects/sample/.liquid-build/configuration: repository has no "
                  "recorded build configuration",
                  repository),
              ".liquid-build/configuration: repository has no recorded build configuration");
    EXPECT_EQ(liquify::without_absolute_paths("/Users/ora/projects/sample: no sync remote",
                                              repository),
              "the repository: no sync remote");
    EXPECT_EQ(liquify::without_absolute_paths(
                  "in rename: No such file [/private/var/folders/x/index/run-179.idx]", {}),
              "in rename: No such file [run-179.idx]");
    EXPECT_EQ(liquify::without_absolute_paths("fetch https://lake.example/v1/x failed", {}),
              "fetch https://lake.example/v1/x failed");
    EXPECT_EQ(liquify::without_absolute_paths("C:\\Users\\ora\\sample\\file.txt: locked", {}),
              "file.txt: locked");
}

} // namespace
