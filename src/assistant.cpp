// SPDX-License-Identifier: MIT

#include "liquify/assistant.hpp"

#include <chrono>
#include <fstream>
#include <mutex>
#include <utility>

#include <boost/json.hpp>

namespace liquify {
namespace {

auto system_prompt(AssistantAction action) -> std::string {
    switch (action) {
    case AssistantAction::ask:
        return "Answer using only the inspected liquid graph context. State when "
               "an omission "
               "prevents a firm answer.";
    case AssistantAction::explain_diagnostic:
        return "Explain the selected build diagnostic using only the inspected "
               "liquid graph "
               "context. Give a concise cause and correction.";
    case AssistantAction::propose_edit:
        return "Return only replacement source text for the focus byte range. Do "
               "not use Markdown "
               "fences or commentary.";
    }
    return {};
}

} // namespace

struct Assistant::State {
    mutable std::mutex mutex;
    AssistantSnapshot snapshot;
    AssistantAction action{AssistantAction::ask};
    SourceRange focus;
};

Assistant::Assistant(AiConfig config, bool key_configured, AiProviderFactory provider_factory)
    : state_(std::make_shared<State>()), config_(std::move(config)),
      provider_factory_(std::move(provider_factory)) {
    state_->snapshot.key_configured = key_configured;
    set_enabled(config_.enabled);
}

Assistant::~Assistant() {
    cancel();
}

void Assistant::set_enabled(bool enabled) {
    if (!enabled) {
        if (provider_) {
            provider_->cancel();
            provider_.reset();
        }
        auto lock = std::lock_guard(state_->mutex);
        state_->snapshot = AssistantSnapshot{
            .enabled = false, .key_configured = state_->snapshot.key_configured, .status = "off"};
        config_.enabled = false;
        return;
    }
    auto key_configured = false;
    {
        auto lock = std::lock_guard(state_->mutex);
        state_->snapshot.enabled = true;
        state_->snapshot.status = state_->snapshot.key_configured ? "ready" : "key not configured";
        key_configured = state_->snapshot.key_configured;
    }
    config_.enabled = true;
    if (!provider_ && key_configured && provider_factory_) {
        provider_ = provider_factory_();
    }
}

void Assistant::submit(AssistantAction action,
                       std::string prompt,
                       ContextBlock context,
                       SourceRange focus) {
    if (!provider_) {
        auto lock = std::lock_guard(state_->mutex);
        state_->snapshot.status = state_->snapshot.enabled ? "key not configured" : "off";
        return;
    }
    {
        auto lock = std::lock_guard(state_->mutex);
        state_->action = action;
        state_->focus = focus;
        state_->snapshot.running = true;
        state_->snapshot.refusal = false;
        state_->snapshot.status = "streaming";
        state_->snapshot.response.clear();
        state_->snapshot.context = context.text;
        state_->snapshot.omissions = std::move(context.omissions);
        state_->snapshot.proposal.reset();
        state_->snapshot.accepted.reset();
    }
    auto request = AiRequest{.model = config_.model,
                             .max_tokens = config_.max_tokens,
                             .system = system_prompt(action),
                             .prompt = std::move(prompt),
                             .context = std::move(context.text)};
    auto weak = std::weak_ptr<State>(state_);
    provider_->complete(
        std::move(request),
        {.delta =
             [weak](std::string_view delta) {
                 if (auto state = weak.lock()) {
                     auto lock = std::lock_guard(state->mutex);
                     if (!state->snapshot.enabled) {
                         return;
                     }
                     state->snapshot.response += delta;
                 }
             },
         .complete =
             [weak](AiResult result) {
                 if (auto state = weak.lock()) {
                     auto lock = std::lock_guard(state->mutex);
                     if (!state->snapshot.enabled) {
                         state->snapshot.running = false;
                         state->snapshot.status = "off";
                         return;
                     }
                     state->snapshot.running = false;
                     state->snapshot.refusal = result.stop_reason == AiStopReason::refusal;
                     if (!result.error.empty()) {
                         state->snapshot.status = "error: " + result.error;
                         return;
                     }
                     state->snapshot.status = std::string(ai_stop_reason_name(result.stop_reason));
                     if (state->snapshot.response.empty()) {
                         state->snapshot.response = std::move(result.text);
                     }
                     if (state->action == AssistantAction::propose_edit &&
                         result.stop_reason != AiStopReason::refusal &&
                         result.stop_reason != AiStopReason::cancelled) {
                         state->snapshot.proposal = SourceEdit{state->focus.start_byte,
                                                               state->focus.end_byte,
                                                               state->snapshot.response};
                     }
                 }
             }});
}

void Assistant::cancel() {
    if (provider_) {
        provider_->cancel();
    }
}

void Assistant::accept() {
    auto lock = std::lock_guard(state_->mutex);
    if (!state_->snapshot.proposal) {
        return;
    }
    state_->snapshot.accepted = std::move(state_->snapshot.proposal);
    state_->snapshot.proposal.reset();
    state_->snapshot.status = "edit accepted";
}

void Assistant::reject() {
    auto lock = std::lock_guard(state_->mutex);
    state_->snapshot.proposal.reset();
    state_->snapshot.accepted.reset();
    state_->snapshot.status = state_->snapshot.enabled ? "edit rejected" : "off";
}

auto Assistant::take_accepted() -> std::optional<SourceEdit> {
    auto lock = std::lock_guard(state_->mutex);
    auto accepted = std::move(state_->snapshot.accepted);
    state_->snapshot.accepted.reset();
    return accepted;
}

auto Assistant::snapshot() const -> AssistantSnapshot {
    auto lock = std::lock_guard(state_->mutex);
    return state_->snapshot;
}

auto record_assisted_edit(const std::filesystem::path& repository_path,
                          const std::filesystem::path& edited_path,
                          const SourceEdit& edit) -> liquid::result<void> {
    const auto log_path = repository_path / ".liquid" / "config.liquify.ai.log";
    std::error_code directory_error;
    std::filesystem::create_directories(log_path.parent_path(), directory_error);
    if (directory_error) {
        return std::unexpected(liquid::error{
            liquid::errc::io, directory_error.message(), log_path.parent_path().string()});
    }
    auto record = boost::json::object{};
    record["assisted"] = true;
    record["path"] = edited_path.generic_string();
    record["start_byte"] = edit.start_byte;
    record["old_end_byte"] = edit.old_end_byte;
    record["at_unix_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    auto output = std::ofstream(log_path, std::ios::app);
    if (!output) {
        return std::unexpected(
            liquid::error{liquid::errc::io, "could not open assisted edit log", log_path.string()});
    }
    output << boost::json::serialize(record) << '\n';
    if (!output) {
        return std::unexpected(liquid::error{
            liquid::errc::io, "could not write assisted edit log", log_path.string()});
    }
    return {};
}

} // namespace liquify
