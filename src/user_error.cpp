#include "liquify/user_error.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <span>
#include <system_error>
#include <vector>

namespace liquify {
namespace {

auto lower(std::string_view text) -> std::string {
    auto output = std::string{text};
    std::ranges::transform(output, output.begin(),
                           [](unsigned char value) { return std::tolower(value); });
    return output;
}

auto contains_any(std::string_view text, std::span<const std::string_view> needles) -> bool {
    const auto folded = lower(text);
    return std::ranges::any_of(needles, [&](std::string_view needle) {
        return folded.find(needle) != std::string::npos;
    });
}

// Operating-system wording for a lake that could not be reached at all. These are the
// messages of the resolver and connect calls on macOS, Linux and Windows.
constexpr auto unreachable_messages = std::array<std::string_view, 15>{
    "nodename nor servname",
    "not known",
    "host not found",
    "no such host",
    "name resolution",
    "name or service",
    "refused",
    "timed out",
    "timeout",
    "network is unreachable",
    "network is down",
    "no route to host",
    "host is down",
    "host is unreachable",
    "not connected",
};

constexpr auto secure_messages = std::array<std::string_view, 7>{
    "tls", "ssl", "certificate", "handshake", "x509", "verify", "system_category",
};

constexpr auto forbidden_messages = std::array<std::string_view, 4>{
    "http 403", "forbidden", "permission", "not allowed",
};

auto path_token_end(std::string_view text, std::size_t begin) -> std::size_t {
    auto end = begin;
    while (end < text.size()) {
        const auto value = text[end];
        if (std::isspace(static_cast<unsigned char>(value)) || value == ']' || value == ')' ||
            value == ',' || value == ';' || value == '\'' || value == '"') {
            break;
        }
        ++end;
    }
    // A trailing colon separates the path from liquid's message.
    while (end > begin && (text[end - 1] == ':' || text[end - 1] == '.')) {
        --end;
    }
    return end;
}

auto starts_absolute_path(std::string_view text, std::size_t at) -> bool {
    if (at > 0) {
        const auto before = text[at - 1];
        if (!std::isspace(static_cast<unsigned char>(before)) && before != '[' && before != '(' &&
            before != '\'' && before != '"' && before != '=') {
            return false;
        }
    }
    if (text[at] == '/') {
        return at + 1 < text.size() && text[at + 1] != '/' &&
               !std::isspace(static_cast<unsigned char>(text[at + 1]));
    }
    return at + 2 < text.size() && std::isalpha(static_cast<unsigned char>(text[at])) &&
           text[at + 1] == ':' && (text[at + 2] == '\\' || text[at + 2] == '/');
}

auto repository_roots(const std::filesystem::path& repository) -> std::vector<std::string> {
    auto roots = std::vector<std::string>{};
    if (repository.empty()) {
        return roots;
    }
    roots.push_back(repository.generic_string());
    auto ignored = std::error_code{};
    const auto canonical = std::filesystem::weakly_canonical(repository, ignored);
    if (!ignored && canonical.generic_string() != roots.front()) {
        roots.push_back(canonical.generic_string());
    }
    // macOS reports /var and /tmp through /private.
    for (const auto& root : std::vector<std::string>(roots)) {
        if (root.starts_with("/private/")) {
            roots.push_back(root.substr(8));
        } else if (root.starts_with("/var/") || root.starts_with("/tmp/")) {
            roots.push_back("/private" + root);
        }
    }
    for (auto& root : roots) {
        while (root.size() > 1 && root.ends_with('/')) {
            root.pop_back();
        }
    }
    return roots;
}

auto reduce_path(std::string_view path, const std::vector<std::string>& roots) -> std::string {
    auto generic = std::string{path};
    std::ranges::replace(generic, '\\', '/');
    for (const auto& root : roots) {
        if (generic == root) {
            return "the repository";
        }
        if (generic.starts_with(root + "/")) {
            return generic.substr(root.size() + 1);
        }
    }
    const auto name = std::filesystem::path{generic}.filename().string();
    return name.empty() ? std::string{"a local file"} : name;
}

} // namespace

auto failure_kind_name(FailureKind kind) -> std::string_view {
    switch (kind) {
    case FailureKind::offline:
        return "offline";
    case FailureKind::secure_connection:
        return "secure connection";
    case FailureKind::interrupted:
        return "interrupted";
    case FailureKind::unauthorized:
        return "unauthorized";
    case FailureKind::forbidden:
        return "forbidden";
    case FailureKind::not_found:
        return "not found";
    case FailureKind::lake_unavailable:
        return "lake unavailable";
    case FailureKind::unsupported_lake:
        return "unsupported lake";
    case FailureKind::refused:
        return "refused";
    case FailureKind::local:
        return "local";
    case FailureKind::other:
        return "other";
    }
    return "other";
}

auto lake_host(std::string_view url) -> std::string {
    const auto scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return std::string{url};
    }
    auto host = url.substr(scheme + 3);
    host = host.substr(0, host.find_first_of("/?#"));
    if (const auto at = host.rfind('@'); at != std::string_view::npos) {
        host = host.substr(at + 1);
    }
    if (!host.starts_with('[')) {
        host = host.substr(0, host.find(':'));
    }
    return host.empty() ? std::string{url} : std::string{host};
}

auto classify_lake_failure(const liquid::error& error) -> FailureKind {
    const auto& message = error.message;
    switch (error.code) {
    case liquid::errc::unauthorized:
        return contains_any(message, forbidden_messages) ? FailureKind::forbidden
                                                         : FailureKind::unauthorized;
    case liquid::errc::not_found:
    case liquid::errc::not_remote:
        return FailureKind::not_found;
    case liquid::errc::busy:
    case liquid::errc::locked:
        return FailureKind::lake_unavailable;
    case liquid::errc::unsupported:
    case liquid::errc::invalid_argument:
    case liquid::errc::corrupt:
    case liquid::errc::conflict:
        return FailureKind::unsupported_lake;
    case liquid::errc::unavailable:
        // liquid's sync endpoint words an HTTP 503 answer this way; the lake answered.
        if (message.starts_with("sync endpoint returned HTTP")) {
            return FailureKind::lake_unavailable;
        }
        if (contains_any(message, unreachable_messages)) {
            return FailureKind::offline;
        }
        // DNS and connect failures carry the messages above; what remains under this code is
        // the TLS handshake, where the lake did answer.
        return FailureKind::secure_connection;
    case liquid::errc::io:
        if (contains_any(message, secure_messages)) {
            return FailureKind::secure_connection;
        }
        if (error.message.starts_with("cannot ") || error.message.starts_with("drive ")) {
            return FailureKind::local;
        }
        return FailureKind::interrupted;
    case liquid::errc::already_exists:
        return FailureKind::local;
    case liquid::errc::not_configured:
    case liquid::errc::internal:
    case liquid::errc::ok:
        return FailureKind::other;
    }
    return FailureKind::other;
}

auto describe_clone_failure(const liquid::error& error, std::string_view remote) -> UserError {
    const auto host = lake_host(remote);
    auto output = UserError{.kind = classify_lake_failure(error), .details = error_details(error)};
    switch (output.kind) {
    case FailureKind::offline:
        output.summary = std::format("Could not reach {}", host);
        output.next_step = "Working offline is normal, and local repositories keep working. "
                           "Check the address and your network, then retry.";
        break;
    case FailureKind::secure_connection:
        output.summary = std::format("{} answered, but a secure connection could not be made", host);
        output.next_step = "This computer could not verify the lake's certificate or agree on "
                           "encryption with it. Check the address, or ask the lake's operator "
                           "about its certificate, then retry.";
        break;
    case FailureKind::interrupted:
        output.summary = std::format("The connection to {} broke during the clone", host);
        output.next_step = "Nothing was kept from this attempt. Retry the clone.";
        break;
    case FailureKind::unauthorized:
        output.summary = std::format("{} did not accept your token", host);
        output.next_step = "Create a token on the lake (Account, then API Tokens), set it as "
                           "LIQUID_TOKEN, and retry.";
        break;
    case FailureKind::forbidden:
        output.summary = std::format("Your token has no access to this repository on {}", host);
        output.next_step = "Ask the repository's owner for access, or use a token from an "
                           "account that has it.";
        break;
    case FailureKind::not_found:
        output.summary = std::format("{} has no repository at this address", host);
        output.next_step = "Copy the address from the repository's page on the lake and retry.";
        break;
    case FailureKind::lake_unavailable:
        output.summary = std::format("{} cannot serve the repository right now", host);
        output.next_step = "The lake answered that it is busy or unavailable. Retry in a moment.";
        break;
    case FailureKind::unsupported_lake:
        output.summary = std::format("{} answered in a way this liquify does not understand", host);
        output.next_step = "The lake and liquify may be different versions. Update liquify, or "
                           "ask the lake's operator which version it runs.";
        break;
    case FailureKind::local:
        output.summary = "The local folder could not be used for the clone";
        output.next_step = "Choose a folder that does not exist yet, or an empty one.";
        break;
    case FailureKind::refused:
    case FailureKind::other:
        output.kind = FailureKind::other;
        output.summary = std::format("The clone from {} did not finish", host);
        output.next_step = "Retry the clone. If it fails again, open Details and report the "
                           "message.";
        break;
    }
    return output;
}

auto describe_clone_target_failure(const liquid::error& error,
                                   const std::filesystem::path& target) -> UserError {
    const auto name = target.filename().empty() ? target.parent_path().filename().string()
                                                : target.filename().string();
    return {
        .kind = FailureKind::local,
        .summary = std::format("The folder {} cannot hold the clone", name),
        .next_step = "Choose a folder that does not exist yet, or an empty one.",
        .details = error_details(error),
    };
}

auto describe_clone_refusal(std::string_view reference, std::string_view reason,
                            std::string_view remote) -> UserError {
    return {
        .kind = FailureKind::refused,
        .summary = std::format("{} refused to send {}", lake_host(remote), reference),
        .next_step = "The lake's rules for this repository did not allow the clone. Ask the "
                     "repository's owner, then retry.",
        .details = std::format("{}: {}", reference, reason),
    };
}

auto describe_build_failure(const liquid::error& error, const std::filesystem::path& repository)
    -> UserError {
    auto output = UserError{.details = error_details(error, repository)};
    if (error.code == liquid::errc::not_configured) {
        output.kind = FailureKind::local;
        output.summary = "This repository has no build set up yet";
        output.next_step = "Choose the command that builds it. liquid records the command once "
                           "and runs it on every build.";
        output.action = ErrorAction::set_up_build;
        output.action_label = "Set up build";
        return output;
    }
    if (error.message == "build daemon is unavailable") {
        output.kind = FailureKind::local;
        output.summary = "The build service is not running for this repository";
        output.next_step = "Reopen the repository to start it, then build again.";
        return output;
    }
    output.kind = FailureKind::other;
    output.summary = "The build did not finish";
    output.next_step = "Fix what Details names, then build again.";
    output.action = ErrorAction::retry;
    output.action_label = "Build again";
    return output;
}

auto describe_push_failure(const liquid::error& error, std::string_view remote,
                           const std::filesystem::path& repository) -> UserError {
    auto output = UserError{.details = error_details(error, repository)};
    if (remote.empty()) {
        output.kind = FailureKind::local;
        output.summary = "This repository has no lake to push to";
        output.next_step = "Choose the lake and the repository on it that this one pushes to.";
        output.action = ErrorAction::set_sync_remote;
        output.action_label = "Set lake";
        return output;
    }
    const auto host = lake_host(remote);
    if (error.code == liquid::errc::conflict &&
        error.message.find("identity") != std::string::npos) {
        output.kind = FailureKind::refused;
        output.summary = std::format("{} holds a different repository", host);
        output.next_step = "That lake repository already has its own history. Push to an empty "
                           "lake repository, or clone this one from lake into a new folder.";
        return output;
    }
    output.kind = classify_lake_failure(error);
    output.action = ErrorAction::retry;
    output.action_label = "Push again";
    switch (output.kind) {
    case FailureKind::offline:
        output.summary = std::format("Could not reach {}", host);
        output.next_step = "Working offline is normal: your saved work stays here and can be "
                           "pushed when the lake is reachable.";
        break;
    case FailureKind::secure_connection:
        output.summary = std::format("{} answered, but a secure connection could not be made", host);
        output.next_step = "Check the lake's address, or ask its operator about its certificate.";
        break;
    case FailureKind::interrupted:
        output.summary = std::format("The connection to {} broke during the push", host);
        output.next_step = "Nothing was lost here. Push again.";
        break;
    case FailureKind::unauthorized:
        output.summary = std::format("{} did not accept your token", host);
        output.next_step = "Create a token on the lake (Account, then API Tokens) and set it "
                           "for this lake in Settings.";
        output.action = ErrorAction::none;
        output.action_label.clear();
        break;
    case FailureKind::forbidden:
        output.summary = std::format("Your token cannot push to this repository on {}", host);
        output.next_step = "Ask the repository's owner for write access.";
        output.action = ErrorAction::none;
        output.action_label.clear();
        break;
    case FailureKind::not_found:
        output.summary = std::format("{} has no repository at the address this one pushes to", host);
        output.next_step = "Choose the lake repository again.";
        output.action = ErrorAction::set_sync_remote;
        output.action_label = "Set lake";
        break;
    case FailureKind::lake_unavailable:
        output.summary = std::format("{} cannot take the push right now", host);
        output.next_step = "The lake answered that it is busy or unavailable. Push again in a "
                           "moment.";
        break;
    case FailureKind::unsupported_lake:
        output.summary = std::format("{} did not accept what this liquify sent", host);
        output.next_step = "The lake and liquify may be different versions. Update liquify, or "
                           "ask the lake's operator which version it runs.";
        output.action = ErrorAction::none;
        output.action_label.clear();
        break;
    case FailureKind::local:
        output.summary = "The push could not read this repository";
        output.next_step = "Fix what Details names, then push again.";
        break;
    case FailureKind::refused:
    case FailureKind::other:
        output.kind = FailureKind::other;
        output.summary = std::format("The push to {} did not finish", host);
        output.next_step = "Push again. If it fails again, open Details and report the message.";
        break;
    }
    return output;
}

auto describe_query_failure(const liquid::error& error, const std::filesystem::path& repository)
    -> UserError {
    return {
        .kind = FailureKind::local,
        .summary = "Code analysis could not update",
        .next_step = "Diagnostics, definitions and completion show their last result. Edit or "
                     "save the file to try again.",
        .details = error_details(error, repository),
    };
}

auto describe_index_failure(std::string_view message, const std::filesystem::path& repository)
    -> UserError {
    return {
        .kind = FailureKind::local,
        .summary = "The file and symbol index could not be built",
        .next_step = "The tree still lists files; Quick Open and symbol search wait for the "
                     "index. Reopen the repository to try again.",
        .details = without_absolute_paths(message, repository),
    };
}

auto describe_folder_failure(std::string_view folder, std::string_view message,
                             const std::filesystem::path& repository) -> UserError {
    return {
        .kind = FailureKind::local,
        .summary = std::format("The folder {} could not be read", folder),
        .next_step = "Collapse and expand it to try again.",
        .details = without_absolute_paths(message, repository),
    };
}

auto without_absolute_paths(std::string_view text, const std::filesystem::path& repository)
    -> std::string {
    const auto roots = repository_roots(repository);
    auto output = std::string{};
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        if (starts_absolute_path(text, index)) {
            const auto end = path_token_end(text, index);
            if (end > index + 1) {
                output += reduce_path(text.substr(index, end - index), roots);
                index = end;
                continue;
            }
        }
        output += text[index];
        ++index;
    }
    return output;
}

auto error_details(const liquid::error& error, const std::filesystem::path& repository)
    -> std::string {
    auto raw = error.context.empty() ? error.message : error.context + ": " + error.message;
    return without_absolute_paths(raw, repository);
}

} // namespace liquify
