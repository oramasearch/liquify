#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace liquify {

struct AiRequest {
    std::string model{"claude-opus-5"};
    std::uint32_t max_tokens{4'096};
    std::string system;
    std::string prompt;
    std::string context;
};

enum class AiStopReason : std::uint8_t {
    end_turn,
    max_tokens,
    stop_sequence,
    refusal,
    cancelled,
    error,
};

struct AiResult {
    std::string text;
    AiStopReason stop_reason{AiStopReason::error};
    std::string error;
};

struct AiCallbacks {
    std::function<void(std::string_view)> delta;
    std::function<void(AiResult)> complete;
};

class AiProvider {
public:
    virtual ~AiProvider() = default;
    virtual void complete(AiRequest request, AiCallbacks callbacks) = 0;
    virtual void cancel() = 0;
};

struct AnthropicEndpoint {
    std::string host{"api.anthropic.com"};
    std::string connect_host;
    std::string port{"443"};
    std::string target{"/v1/messages"};
    std::string certificate_authority;
};

class AnthropicProvider final : public AiProvider {
public:
    struct State;

    explicit AnthropicProvider(std::string api_key, AnthropicEndpoint endpoint = {});
    ~AnthropicProvider() override;
    AnthropicProvider(const AnthropicProvider&) = delete;
    auto operator=(const AnthropicProvider&) -> AnthropicProvider& = delete;

    void complete(AiRequest request, AiCallbacks callbacks) override;
    void cancel() override;

private:
    std::shared_ptr<State> state_;
};

[[nodiscard]] auto ai_stop_reason_name(AiStopReason reason) -> std::string_view;

// The Messages API body for `request` (liquify#73). The repository context goes in its own content
// block, inside <repository_context> tags, with the request marker and the closing tag neutralised,
// so repository text cannot stand where the user's request goes; the request follows in a second
// block. The system prompt tells the model the context is data, never instructions.
[[nodiscard]] auto anthropic_request_body(const AiRequest& request) -> std::string;

// `context` made safe to place inside <repository_context>: every "[user-request]" and every
// "</repository_context>" in it is rewritten so it can no longer end the block or start a request.
[[nodiscard]] auto quote_repository_context(std::string_view context) -> std::string;

} // namespace liquify
