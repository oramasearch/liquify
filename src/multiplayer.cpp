// SPDX-License-Identifier: MIT

#include "liquify/multiplayer.hpp"

#include <chrono>
#include <random>
#include <utility>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

namespace liquify {
namespace {

auto error_message(const liquid::error& error) -> std::string {
    if (error.context.empty()) {
        return error.message;
    }
    return error.message + ": " + error.context;
}

auto activity_name(liquid::presence_activity value) -> std::string_view {
    switch (value) {
    case liquid::presence_activity::editing:
        return "editing";
    case liquid::presence_activity::reading:
        return "reading";
    case liquid::presence_activity::idle:
        return "idle";
    }
    return "idle";
}

auto lease_kind_name(liquid::lease_kind value) -> std::string_view {
    switch (value) {
    case liquid::lease_kind::file:
        return "file";
    case liquid::lease_kind::symbol:
        return "symbol";
    case liquid::lease_kind::range:
        return "range";
    }
    return "file";
}

} // namespace

auto make_multiplayer_session() -> liquid::session_id {
    auto result = liquid::session_id{};
    auto generator = std::random_device{};
    for (auto& byte : result.bytes) {
        byte = std::byte{static_cast<unsigned char>(generator())};
    }
    return result;
}

auto refresh_multiplayer(const std::filesystem::path& repository_path,
                         std::string path,
                         liquid::session_id session,
                         liquid::frontend_source_range range)
    -> liquid::result<MultiplayerSnapshot> {
    auto options = liquid::repository_options{};
    options.session = session;
    auto repository = liquid::runtime::block_on(liquid::repository::open(repository_path, options));
    if (!repository) {
        return std::unexpected(repository.error());
    }
    auto reported = liquid::runtime::block_on(repository->report_presence(
        {.path = path, .range = range, .activity = liquid::presence_activity::editing}));
    if (!reported) {
        return std::unexpected(reported.error());
    }
    auto change = liquid::runtime::block_on(repository->head());
    if (!change) {
        return std::unexpected(change.error());
    }
    auto attribution = liquid::runtime::block_on(repository->attribution(path, *change));
    if (!attribution) {
        return std::unexpected(attribution.error());
    }
    auto presence = liquid::runtime::block_on(repository->presence());
    if (!presence) {
        return std::unexpected(presence.error());
    }
    auto leases = liquid::runtime::block_on(repository->leases());
    if (!leases) {
        return std::unexpected(leases.error());
    }
    return MultiplayerSnapshot{
        session, std::move(*presence), std::move(*leases), std::move(*attribution), *change};
}

auto acquire_multiplayer_lease(const std::filesystem::path& repository_path,
                               liquid::session_id session,
                               liquid::lease_claim claim) -> liquid::result<void> {
    auto options = liquid::repository_options{};
    options.session = session;
    auto repository = liquid::runtime::block_on(liquid::repository::open(repository_path, options));
    if (!repository) {
        return std::unexpected(repository.error());
    }
    auto acquired =
        liquid::runtime::block_on(repository->lease(std::move(claim), std::chrono::minutes{5}));
    if (!acquired) {
        return std::unexpected(acquired.error());
    }
    return {};
}

auto release_multiplayer_lease(const std::filesystem::path& repository_path,
                               liquid::session_id session,
                               liquid::lease_claim claim) -> liquid::result<void> {
    auto options = liquid::repository_options{};
    options.session = session;
    auto repository = liquid::runtime::block_on(liquid::repository::open(repository_path, options));
    if (!repository) {
        return std::unexpected(repository.error());
    }
    auto released = liquid::runtime::block_on(repository->release(std::move(claim)));
    if (!released) {
        return std::unexpected(released.error());
    }
    return {};
}

auto multiplayer_presence_text(const liquid::presence& value) -> std::string {
    return value.actor.taap_id + " is " + std::string(activity_name(value.activity)) + " in " +
           value.path + " (session " + liquid::to_string(value.session) + ")";
}

auto multiplayer_lease_text(const liquid::semantic_lease& value) -> std::string {
    return value.by.taap_id + " holds a " + std::string(lease_kind_name(value.claim.kind)) +
           " lease for " + value.claim.path + " (session " + liquid::to_string(value.session) + ")";
}

auto multiplayer_attribution_text(const liquid::line_attribution& value) -> std::string {
    return "line " + std::to_string(value.line) + ": " + value.by.taap_id +
           (value.agent ? " (agent)" : "") + " / session " + liquid::to_string(value.session) +
           " / change " + liquid::to_string(value.change.value);
}

} // namespace liquify
