// SPDX-License-Identifier: MIT

#include "liquify/lake_remote.hpp"

#include <cstdlib>

#include "liquid/drive.hpp"
#include "liquid/runtime.hpp"

namespace liquify {

auto lake_token() -> std::string {
    const auto* token = std::getenv("LIQUID_TOKEN");
    return token == nullptr ? std::string{} : std::string{token};
}

auto recorded_lake_remote(const std::filesystem::path& repository)
    -> liquid::result<std::string> {
    auto client = liquid::runtime::block_on(liquid::drive_client::open(repository));
    if (!client) {
        if (client.error().code == liquid::errc::not_found &&
            client.error().message == "folder has no configured drive remote") {
            return std::string{};
        }
        return std::unexpected(client.error());
    }
    auto status = liquid::runtime::block_on(client->status());
    if (!status) {
        return std::unexpected(status.error());
    }
    return status->remote;
}

auto record_lake_remote(const std::filesystem::path& repository, const std::string& remote)
    -> UserError {
    if (!remote.starts_with("https://") && !remote.starts_with("http://")) {
        return {.kind = FailureKind::local,
                .summary = "That is not a lake repository address",
                .next_step = "Copy the address from the repository's page on lake; it starts "
                             "with https://."};
    }
    auto options = liquid::drive_options{};
    options.bearer_token = lake_token();
    auto client = liquid::runtime::block_on(
        liquid::drive_client::open(repository, remote, std::move(options)));
    if (!client) {
        if (client.error().code == liquid::errc::conflict) {
            return {.kind = FailureKind::local,
                    .summary = "This repository already pushes to another lake repository",
                    .next_step = "Clone the other repository into a new folder instead.",
                    .details = error_details(client.error(), repository)};
        }
        return {.kind = FailureKind::local,
                .summary = "The lake could not be recorded for this repository",
                .next_step = "Try again. Your work is unchanged.",
                .details = error_details(client.error(), repository)};
    }
    return {};
}

} // namespace liquify
