#include "liquify/ai_provider.hpp"

#include <array>
#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/connect.hpp>
#include <boost/corosio/openssl_stream.hpp>
#include <boost/corosio/resolver.hpp>
#include <boost/corosio/tcp_socket.hpp>
#include <boost/corosio/tls_context.hpp>
#include <boost/http.hpp>
#include <boost/http/response_parser.hpp>
#include <boost/json.hpp>

#include "liquid/error.hpp"
#include "liquid/runtime.hpp"

namespace liquify {
namespace {

constexpr auto anthropic_version = "2023-06-01";

auto provider_error(std::string message, std::string context = {}) -> liquid::error {
    return {liquid::errc::unavailable, std::move(message), std::move(context)};
}

auto request_body(const AiRequest& request) -> std::string {
    auto message = boost::json::object{};
    message["role"] = "user";
    message["content"] = request.context + "\n\n[user-request]\n" + request.prompt;
    auto messages = boost::json::array{};
    messages.push_back(std::move(message));

    auto body = boost::json::object{};
    body["model"] = request.model;
    body["max_tokens"] = request.max_tokens;
    body["system"] = request.system;
    body["messages"] = std::move(messages);
    body["stream"] = true;
    return boost::json::serialize(body);
}

auto request_wire(const AiRequest& input,
                  const AnthropicEndpoint& endpoint,
                  std::string_view api_key) -> std::string {
    const auto body = request_body(input);
    auto request = boost::http::request{boost::http::method::post, endpoint.target};
    auto authority = endpoint.host;
    if (endpoint.port != "443") {
        authority += ":" + endpoint.port;
    }
    request.set(boost::http::field::host, authority);
    request.set(boost::http::field::content_type, "application/json");
    request.set(boost::http::field::accept, "text/event-stream");
    request.set(boost::http::field::connection, "close");
    request.set("x-api-key", api_key);
    request.set("anthropic-version", anthropic_version);
    request.set_payload_size(body.size());
    auto wire = std::string(request.buffer());
    wire += body;
    return wire;
}

struct SseEvent {
    std::string name;
    std::string data;
};

class SseDecoder {
public:
    void append(std::string_view bytes) {
        buffer_.append(bytes);
    }

    [[nodiscard]] auto next() -> std::optional<SseEvent> {
        auto end = buffer_.find("\n\n");
        auto delimiter = std::size_t{2};
        const auto crlf_end = buffer_.find("\r\n\r\n");
        if (crlf_end != std::string::npos && (end == std::string::npos || crlf_end < end)) {
            end = crlf_end;
            delimiter = 4;
        }
        if (end == std::string::npos) {
            return std::nullopt;
        }
        auto block = buffer_.substr(0, end);
        buffer_.erase(0, end + delimiter);
        auto output = SseEvent{};
        auto offset = std::size_t{};
        while (offset <= block.size()) {
            auto line_end = block.find('\n', offset);
            if (line_end == std::string::npos) {
                line_end = block.size();
            }
            auto line = std::string_view(block).substr(offset, line_end - offset);
            if (line.ends_with('\r')) {
                line.remove_suffix(1);
            }
            if (line.starts_with("event:")) {
                output.name = field_value(line.substr(6));
            } else if (line.starts_with("data:")) {
                if (!output.data.empty()) {
                    output.data.push_back('\n');
                }
                output.data += field_value(line.substr(5));
            }
            if (line_end == block.size()) {
                break;
            }
            offset = line_end + 1;
        }
        return output;
    }

private:
    static auto field_value(std::string_view value) -> std::string {
        if (value.starts_with(' ')) {
            value.remove_prefix(1);
        }
        return std::string(value);
    }

    std::string buffer_;
};

auto json_string(const boost::json::object& object, std::string_view key)
    -> std::optional<std::string> {
    const auto* value = object.if_contains(key);
    if (value == nullptr || !value->is_string()) {
        return std::nullopt;
    }
    return std::string(value->as_string());
}

auto stop_reason(std::string_view value) -> AiStopReason {
    if (value == "end_turn") {
        return AiStopReason::end_turn;
    }
    if (value == "max_tokens") {
        return AiStopReason::max_tokens;
    }
    if (value == "stop_sequence") {
        return AiStopReason::stop_sequence;
    }
    if (value == "refusal") {
        return AiStopReason::refusal;
    }
    return AiStopReason::error;
}

auto process_event(const SseEvent& event,
                   AiResult& result,
                   const AiCallbacks& callbacks,
                   bool& message_stopped) -> liquid::result<void> {
    if (event.data.empty() || event.data == "[DONE]") {
        return {};
    }
    auto parsed = boost::json::parse(event.data);
    if (!parsed.is_object()) {
        return std::unexpected(provider_error("SSE data is not a JSON object"));
    }
    const auto& object = parsed.as_object();
    const auto type = json_string(object, "type").value_or(event.name);
    if (type == "content_block_delta") {
        const auto* delta = object.if_contains("delta");
        if (delta == nullptr || !delta->is_object()) {
            return std::unexpected(provider_error("content delta is missing its delta object"));
        }
        const auto text = json_string(delta->as_object(), "text");
        if (text) {
            result.text += *text;
            if (callbacks.delta) {
                callbacks.delta(*text);
            }
        }
    } else if (type == "message_delta") {
        const auto* delta = object.if_contains("delta");
        if (delta != nullptr && delta->is_object()) {
            const auto reason = json_string(delta->as_object(), "stop_reason");
            if (reason) {
                result.stop_reason = stop_reason(*reason);
            }
        }
    } else if (type == "message_stop") {
        message_stopped = true;
        if (result.stop_reason == AiStopReason::error) {
            result.stop_reason = AiStopReason::end_turn;
        }
    } else if (type == "error") {
        return std::unexpected(provider_error("Anthropic returned an SSE error"));
    }
    return {};
}

} // namespace

struct AnthropicProvider::State {
    std::mutex mutex;
    std::shared_ptr<boost::corosio::tcp_socket> socket;
    std::string api_key;
    AnthropicEndpoint endpoint;
    std::atomic<bool> cancelled{};
    std::atomic<bool> running{};
};

namespace {

template <boost::capy::Stream Stream>
auto exchange(Stream& stream,
              std::string wire,
              const std::shared_ptr<AnthropicProvider::State>& state,
              const AiCallbacks& callbacks) -> liquid::runtime::task<AiResult> {
    auto [write_error, written] =
        co_await boost::capy::write(stream, boost::capy::const_buffer(wire.data(), wire.size()));
    if (write_error || written != wire.size()) {
        co_return std::unexpected(provider_error(
            write_error ? write_error.message() : "short HTTPS write", "Anthropic Messages API"));
    }

    auto parser_config = boost::http::make_parser_config(boost::http::parser_config{false});
    auto parser = boost::http::response_parser(parser_config);
    parser.reset();
    parser.start();
    auto [header_error] = co_await parser.read_header(stream);
    if (header_error) {
        co_return std::unexpected(
            provider_error(header_error.message(), "Anthropic response header"));
    }
    if (static_cast<unsigned>(parser.get().status()) < 200 ||
        static_cast<unsigned>(parser.get().status()) >= 300) {
        co_return std::unexpected(
            provider_error("Anthropic returned HTTP " +
                               std::to_string(static_cast<unsigned>(parser.get().status())),
                           "Anthropic Messages API"));
    }

    auto result = AiResult{};
    auto decoder = SseDecoder{};
    auto source = parser.source_for(stream);
    auto buffers = std::array<boost::capy::const_buffer, 16>{};
    auto message_stopped = false;
    while (!message_stopped) {
        if (state->cancelled.load()) {
            result.stop_reason = AiStopReason::cancelled;
            co_return result;
        }
        auto [read_error, pulled] = co_await source.pull(buffers);
        auto bytes = std::size_t{};
        for (const auto buffer : pulled) {
            decoder.append({static_cast<const char*>(buffer.data()), buffer.size()});
            bytes += buffer.size();
        }
        source.consume(bytes);
        while (auto event = decoder.next()) {
            auto processed = process_event(*event, result, callbacks, message_stopped);
            if (!processed) {
                co_return std::unexpected(processed.error());
            }
        }
        if (read_error) {
            if (state->cancelled.load()) {
                result.stop_reason = AiStopReason::cancelled;
                co_return result;
            }
            co_return std::unexpected(provider_error(
                message_stopped ? read_error.message() : "stream disconnected before message_stop",
                "Anthropic Messages API"));
        }
        if (pulled.empty() && !message_stopped) {
            co_return std::unexpected(provider_error("stream disconnected before message_stop",
                                                     "Anthropic Messages API"));
        }
    }
    co_return result;
}

} // namespace

namespace {

auto run_request(std::shared_ptr<AnthropicProvider::State> state,
                 AiRequest request,
                 AiCallbacks callbacks) -> liquid::runtime::task<void> {
    auto completed = AiResult{};
    try {
        const auto executor = co_await boost::capy::this_coro::executor;
        auto resolver = boost::corosio::resolver{executor.context()};
        const auto& connect_host = state->endpoint.connect_host.empty()
                                       ? state->endpoint.host
                                       : state->endpoint.connect_host;
        auto [resolve_error, endpoints] =
            co_await resolver.resolve(connect_host, state->endpoint.port);
        if (resolve_error) {
            completed.error = resolve_error.message();
        } else if (state->cancelled.load()) {
            completed.stop_reason = AiStopReason::cancelled;
        } else {
            auto socket = std::make_shared<boost::corosio::tcp_socket>(executor.context());
            {
                auto lock = std::lock_guard(state->mutex);
                state->socket = socket;
            }
            auto [connect_error, endpoint] = co_await boost::corosio::connect(*socket, endpoints);
            static_cast<void>(endpoint);
            if (connect_error) {
                completed.error = connect_error.message();
            } else if (state->cancelled.load()) {
                completed.stop_reason = AiStopReason::cancelled;
            } else {
                auto tls = boost::corosio::tls_context{};
                tls.set_hostname(state->endpoint.host);
                auto trust_error =
                    state->endpoint.certificate_authority.empty()
                        ? tls.set_default_verify_paths()
                        : tls.add_certificate_authority(state->endpoint.certificate_authority);
                if (!trust_error) {
                    trust_error = tls.set_verify_mode(boost::corosio::tls_verify_mode::peer);
                }
                if (trust_error) {
                    completed.error = trust_error.message();
                } else {
                    auto secure = boost::corosio::openssl_stream{socket.get(), tls};
                    auto [handshake_error] =
                        co_await secure.handshake(boost::corosio::openssl_stream::client);
                    if (handshake_error) {
                        completed.error = handshake_error.message();
                    } else {
                        auto exchanged = co_await exchange(
                            secure,
                            request_wire(request, state->endpoint, state->api_key),
                            state,
                            callbacks);
                        if (exchanged) {
                            completed = std::move(*exchanged);
                        } else {
                            completed.error = exchanged.error().message;
                        }
                        auto [shutdown_error] = co_await secure.shutdown();
                        static_cast<void>(shutdown_error);
                    }
                }
            }
        }
    } catch (const std::exception& exception) {
        completed.error = exception.what();
    } catch (...) {
        completed.error = "unknown provider failure";
    }
    if (!completed.error.empty()) {
        completed.stop_reason = AiStopReason::error;
    }
    {
        auto lock = std::lock_guard(state->mutex);
        state->socket.reset();
    }
    state->running.store(false);
    if (callbacks.complete) {
        callbacks.complete(std::move(completed));
    }
    co_return liquid::result<void>{};
}

} // namespace

AnthropicProvider::AnthropicProvider(std::string api_key, AnthropicEndpoint endpoint)
    : state_(std::make_shared<State>()) {
    state_->api_key = std::move(api_key);
    state_->endpoint = std::move(endpoint);
}

AnthropicProvider::~AnthropicProvider() {
    cancel();
}

void AnthropicProvider::complete(AiRequest request, AiCallbacks callbacks) {
    if (state_->running.exchange(true)) {
        if (callbacks.complete) {
            callbacks.complete(AiResult{.stop_reason = AiStopReason::error,
                                        .error = "provider request already running"});
        }
        return;
    }
    state_->cancelled.store(false);
    liquid::runtime::spawn(run_request(state_, std::move(request), std::move(callbacks)), {});
}

void AnthropicProvider::cancel() {
    state_->cancelled.store(true);
    auto lock = std::lock_guard(state_->mutex);
    if (state_->socket) {
        state_->socket->cancel();
    }
}

auto ai_stop_reason_name(AiStopReason reason) -> std::string_view {
    switch (reason) {
    case AiStopReason::end_turn:
        return "complete";
    case AiStopReason::max_tokens:
        return "maximum tokens";
    case AiStopReason::stop_sequence:
        return "stop sequence";
    case AiStopReason::refusal:
        return "refusal";
    case AiStopReason::cancelled:
        return "cancelled";
    case AiStopReason::error:
        return "error";
    }
    return "error";
}

} // namespace liquify
