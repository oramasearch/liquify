#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "liquid/error.hpp"

namespace liquify {

// What kind of failure a person is looking at. The kind decides the words and the next step;
// it is derived from liquid's error code, never from where in liquify the failure surfaced.
enum class FailureKind : std::uint8_t {
    offline,           // the lake could not be reached: name lookup, refused, timed out
    secure_connection, // the lake answered, but no secure connection could be made
    interrupted,       // the connection broke after it was made
    unauthorized,      // the lake did not accept the token
    forbidden,         // the token is valid but has no access to this repository
    not_found,         // the lake has no such repository
    lake_unavailable,  // the lake answered that it cannot serve the request now
    unsupported_lake,  // the lake answered in a way this liquify does not understand
    refused,           // the lake refused a reference update
    local,             // the failure is on this computer (a folder, a file)
    other,
};

// The action that fixes a failure, drawn as a button beside the sentence when liquify offers it.
enum class ErrorAction : std::uint8_t {
    none,
    set_sync_remote, // choose the lake and repository this one pushes to
    set_up_build,    // record the build command
    retry,
};

// One failure as the window shows it: a sentence in the product's words, the step that fixes
// it, and liquid's own message for the Details control. No field carries an absolute path
// the person did not type.
struct UserError {
    FailureKind kind{FailureKind::other};
    std::string summary;
    std::string next_step;
    std::string details;
    ErrorAction action{ErrorAction::none};
    std::string action_label;

    [[nodiscard]] auto empty() const noexcept -> bool { return summary.empty(); }
    // Only an unreachable lake is "offline"; every other failure means the lake answered.
    [[nodiscard]] auto offline() const noexcept -> bool { return kind == FailureKind::offline; }
};

[[nodiscard]] auto failure_kind_name(FailureKind kind) -> std::string_view;

// The host of a lake URL ("https://lake.example:8443/v1/repositories/x" -> "lake.example"),
// or the whole text when it is not a URL.
[[nodiscard]] auto lake_host(std::string_view url) -> std::string;

// Classifies a failure of a network operation against a lake from liquid's error code. The
// transport reports DNS, connection and TLS handshake failures with one code (`unavailable`),
// so only those are told apart by the operating system's message.
[[nodiscard]] auto classify_lake_failure(const liquid::error& error) -> FailureKind;

// A failed clone from `remote`.
[[nodiscard]] auto describe_clone_failure(const liquid::error& error, std::string_view remote)
    -> UserError;
// A clone whose local folder cannot be used (it exists and is not empty, or is a file).
[[nodiscard]] auto describe_clone_target_failure(const liquid::error& error,
                                                 const std::filesystem::path& target)
    -> UserError;
// A clone the lake refused for one reference.
[[nodiscard]] auto describe_clone_refusal(std::string_view reference, std::string_view reason,
                                          std::string_view remote) -> UserError;

// A failed build or test run (F7, F8). A repository with no recorded build offers Set up build.
[[nodiscard]] auto describe_build_failure(const liquid::error& error,
                                          const std::filesystem::path& repository) -> UserError;
// A failed push to lake (F9). A repository with no sync remote offers Set lake.
[[nodiscard]] auto describe_push_failure(const liquid::error& error, std::string_view remote,
                                         const std::filesystem::path& repository) -> UserError;
// A failed editor query (diagnostics, hover, definition, completion).
[[nodiscard]] auto describe_query_failure(const liquid::error& error,
                                          const std::filesystem::path& repository) -> UserError;
// A failed read of the repository's path and symbol index, or of one folder.
[[nodiscard]] auto describe_index_failure(std::string_view message,
                                          const std::filesystem::path& repository) -> UserError;
[[nodiscard]] auto describe_folder_failure(std::string_view folder, std::string_view message,
                                           const std::filesystem::path& repository) -> UserError;

// liquid's raw message for Details: "context: message", with `repository` shown relative to
// the repository and any other absolute path reduced to its file name.
[[nodiscard]] auto error_details(const liquid::error& error,
                                 const std::filesystem::path& repository = {}) -> std::string;
[[nodiscard]] auto without_absolute_paths(std::string_view text,
                                          const std::filesystem::path& repository = {})
    -> std::string;

} // namespace liquify
