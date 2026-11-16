// SPDX-License-Identifier: MIT

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

namespace {

TEST(UserError, ABuildWithNoRecordedCommandOffersSetUpBuild) {
    const auto repository = std::filesystem::path{"/Users/ora/projects/sample"};
    const auto failure = liquify::describe_build_failure(
        {liquid::errc::not_configured,
         "repository has no recorded build configuration; run liquid wrap <command> first",
         "/Users/ora/projects/sample/.liquid-build/configuration"},
        repository);
    EXPECT_EQ(failure.summary, "This repository has no build set up yet");
    EXPECT_EQ(failure.action, liquify::ErrorAction::set_up_build);
    EXPECT_EQ(failure.details.find("/Users/"), std::string::npos);
    EXPECT_NE(failure.details.find(".liquid-build/configuration"), std::string::npos);
}

TEST(UserError, APushWithNoSyncRemoteOffersSetLake) {
    const auto failure = liquify::describe_push_failure(
        {liquid::errc::not_configured, "repository has no configured sync remote", ""}, "",
        "/Users/ora/projects/sample");
    EXPECT_EQ(failure.summary, "This repository has no lake to push to");
    EXPECT_EQ(failure.action, liquify::ErrorAction::set_sync_remote);
    EXPECT_FALSE(failure.offline());
}

TEST(UserError, APushToAnUnreachableLakeIsOfflineAndRetryable) {
    const auto failure = liquify::describe_push_failure(
        {liquid::errc::unavailable, "Connection refused", "sync endpoint"},
        "https://lake.example/v1/repositories/abc", {});
    EXPECT_TRUE(failure.offline());
    EXPECT_EQ(failure.action, liquify::ErrorAction::retry);
    EXPECT_EQ(failure.summary, "Could not reach lake.example");
}

TEST(UserError, NoComponentTextCarriesTheRepositoryPath) {
    const auto repository = std::filesystem::path{"/private/var/folders/x/sample"};
    for (const auto& failure :
         {liquify::describe_query_failure(
              {liquid::errc::io, "cannot read", "/private/var/folders/x/sample/src/a.cpp"},
              repository),
          liquify::describe_index_failure(
              "filesystem error: in rename: No such file or directory "
              "[/var/folders/x/sample/.liquid/index/run-179.idx.backend-tmp]",
              repository),
          liquify::describe_folder_failure("src", "/private/var/folders/x/sample/src: denied",
                                           repository)}) {
        EXPECT_EQ(failure.summary.find("/var/"), std::string::npos) << failure.summary;
        EXPECT_EQ(failure.details.find("/var/"), std::string::npos) << failure.details;
        EXPECT_FALSE(failure.next_step.empty());
    }
}

} // namespace
