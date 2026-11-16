// SPDX-License-Identifier: MIT

#include "liquify/change_cut.hpp"

#include <algorithm>
#include <optional>
#include <set>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

namespace liquify {
namespace {

auto short_id(const liquid::digest& value) -> std::string {
    auto text = liquid::to_string(value);
    const auto separator = text.find(':');
    if (separator != std::string::npos) {
        text = text.substr(separator + 1);
    }
    return text.substr(0, 12);
}

auto open_repository(const std::filesystem::path& path, liquid::session_id session)
    -> liquid::result<liquid::repository> {
    auto options = liquid::repository_options{};
    options.session = session;
    return liquid::runtime::block_on(liquid::repository::open(path, options));
}

auto local_failure(std::string summary, std::string next_step, const liquid::error& error,
                   const std::filesystem::path& repository) -> UserError {
    return {.kind = FailureKind::local,
            .summary = std::move(summary),
            .next_step = std::move(next_step),
            .details = error_details(error, repository)};
}

} // namespace

auto cut_message_summary(std::string_view message) -> std::string {
    auto line = message.substr(0, message.find('\n'));
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = line.find_last_not_of(" \t\r");
    return std::string{line.substr(first, last - first + 1)};
}

auto load_pending_change(const std::filesystem::path& repository) -> PendingChange {
    auto output = PendingChange{};
    auto opened = open_repository(repository, {});
    if (!opened) {
        output.failure = local_failure("The repository could not be read",
                                       "Close the composer and try again.", opened.error(),
                                       repository);
        return output;
    }
    // Drips record the change they were written on. Those written on the current head are
    // the ones a cut gathers; earlier drips already belong to a change.
    auto head = std::optional<liquid::digest>{};
    if (const auto status = opened->status(); status && status->head) {
        head = status->head->value;
        output.base = short_id(*head);
    }
    const auto on_head = [&](const liquid::drip& drip) {
        if (!head) {
            return std::ranges::all_of(drip.base.value.bytes,
                                       [](std::byte value) { return value == std::byte{}; });
        }
        return drip.base.value.alg == head->alg && drip.base.value.bytes == head->bytes;
    };
    auto drips = liquid::runtime::block_on(opened->drips());
    if (!drips) {
        output.failure = local_failure("The saved drips could not be read",
                                       "Close the composer and try again.", drips.error(),
                                       repository);
        return output;
    }
    auto paths = std::set<std::string>{};
    for (const auto& drip : *drips) {
        if (!on_head(drip)) {
            continue;
        }
        auto entry = PendingDrip{.id = short_id(drip.id.value),
                                 .author = std::string{drip.by.identifier()},
                                 .at = drip.at};
        for (const auto& op : drip.ops) {
            entry.paths.push_back(op.path);
            paths.insert(op.path);
        }
        output.drips.push_back(std::move(entry));
    }
    output.paths.assign(paths.begin(), paths.end());
    return output;
}

auto cut_change(const std::filesystem::path& repository, std::string message,
                liquid::session_id session) -> CutResult {
    auto output = CutResult{};
    if (cut_message_summary(message).empty()) {
        output.failure = {.kind = FailureKind::local,
                          .summary = "A change needs a message",
                          .next_step = "Say in one line what the change does."};
        return output;
    }
    auto opened = open_repository(repository, session);
    if (!opened) {
        output.failure = local_failure("The repository could not be opened for the cut",
                                       "Try again.", opened.error(), repository);
        return output;
    }
    auto cut = liquid::runtime::block_on(opened->cut(std::move(message)));
    if (!cut) {
        output.failure = local_failure("The change could not be cut",
                                       "Your saved drips are unchanged. Try again.",
                                       cut.error(), repository);
        return output;
    }
    output.change = liquid::to_string(cut->value);
    return output;
}

} // namespace liquify
