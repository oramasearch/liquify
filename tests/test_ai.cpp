#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/endpoint.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/corosio/openssl_stream.hpp>
#include <boost/corosio/socket_option.hpp>
#include <boost/corosio/tcp.hpp>
#include <boost/corosio/tcp_acceptor.hpp>
#include <boost/corosio/tcp_socket.hpp>
#include <gtest/gtest.h>

#include "../src/editor_buffer.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/ai_provider.hpp"
#include "liquify/assistant.hpp"
#include "liquify/context_builder.hpp"
#include "liquify/interactive_analysis.hpp"

namespace {

using namespace std::chrono_literals;

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view label) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                (std::string(label) + "-" + std::to_string(suffix));
        std::filesystem::remove_all(path_);
    }

    ~TemporaryDirectory() {
        auto error = std::error_code{};
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path& {
        return path_;
    }

private:
    std::filesystem::path path_;
};

auto bytes(std::string_view value) -> std::vector<std::byte> {
    const auto characters = std::span(value.data(), value.size());
    const auto source = std::as_bytes(characters);
    return {source.begin(), source.end()};
}

auto read_text(const std::filesystem::path& path) -> std::string {
    auto input = std::ifstream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class ImmediateProvider final : public liquify::AiProvider {
public:
    explicit ImmediateProvider(std::string response) : response_(std::move(response)) {}

    void complete(liquify::AiRequest, liquify::AiCallbacks callbacks) override {
        if (callbacks.delta) {
            callbacks.delta(response_);
        }
        if (callbacks.complete) {
            callbacks.complete({.text = response_, .stop_reason = liquify::AiStopReason::end_turn});
        }
    }

    void cancel() override {}

private:
    std::string response_;
};

class HttpsDouble {
public:
    HttpsDouble(std::string body, bool disconnect)
        : acceptor_(context_), socket_(context_), body_(std::move(body)), disconnect_(disconnect),
          certificate_(read_text(std::filesystem::path(LIQUIFY_SOURCE_DIR) /
                                 "modules/liquid/tools/reapi-peers/nativelink-tls-cert.pem")),
          private_key_(read_text(std::filesystem::path(LIQUIFY_SOURCE_DIR) /
                                 "modules/liquid/tools/reapi-peers/nativelink-tls-key.pem")) {
        acceptor_.open(boost::corosio::tcp::v4());
        acceptor_.set_option(boost::corosio::socket_option::reuse_address(true));
        const auto bind_error =
            acceptor_.bind(boost::corosio::endpoint(boost::corosio::ipv4_address::loopback(), 0));
        if (bind_error) {
            error_ = bind_error.message();
            return;
        }
        const auto listen_error = acceptor_.listen();
        if (listen_error) {
            error_ = listen_error.message();
        }
    }

    [[nodiscard]] auto endpoint() const -> liquify::AnthropicEndpoint {
        return {.host = "localhost",
                .connect_host = "127.0.0.1",
                .port = std::to_string(acceptor_.local_endpoint().port()),
                .target = "/v1/messages",
                .certificate_authority = certificate_};
    }

    void run() {
        boost::capy::run_async(context_.get_executor())(serve());
        context_.run();
    }

    void count_connections_while_idle() {
        boost::capy::run_async(context_.get_executor())(serve());
        context_.run_for(25ms);
        acceptor_.cancel();
        context_.run();
    }

    [[nodiscard]] auto connections() const -> std::uint32_t {
        return connections_.load();
    }

    [[nodiscard]] auto request() const -> const std::string& {
        return request_;
    }

    [[nodiscard]] auto error() const -> const std::string& {
        return error_;
    }

private:
    auto serve() -> boost::capy::task<> {
        auto [accept_error] = co_await acceptor_.accept(socket_);
        if (accept_error) {
            error_ = accept_error.message();
            co_return;
        }
        connections_.fetch_add(1);
        auto tls_context = boost::corosio::tls_context{};
        const auto certificate_error =
            tls_context.use_certificate(certificate_, boost::corosio::tls_file_format::pem);
        const auto key_error =
            tls_context.use_private_key(private_key_, boost::corosio::tls_file_format::pem);
        if (certificate_error || key_error) {
            error_ = certificate_error ? certificate_error.message() : key_error.message();
            co_return;
        }
        tls_context.set_verify_mode(boost::corosio::tls_verify_mode::none);
        auto secure = boost::corosio::openssl_stream(&socket_, tls_context);
        auto [handshake_error] = co_await secure.handshake(boost::corosio::openssl_stream::server);
        if (handshake_error) {
            error_ = handshake_error.message();
            co_return;
        }
        auto buffer = std::array<char, 4096>{};
        while (!request_complete()) {
            auto [read_error, size] = co_await secure.read_some(
                boost::capy::mutable_buffer(buffer.data(), buffer.size()));
            if (read_error) {
                error_ = read_error.message();
                co_return;
            }
            request_.append(buffer.data(), size);
        }
        auto response = std::string("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n");
        response += "Transfer-Encoding: chunked\r\n";
        response += "Connection: close\r\n\r\n";
        response += std::format("{:x}\r\n", body_.size());
        response += body_;
        response += "\r\n";
        if (!disconnect_) {
            response += "0\r\n\r\n";
        }
        auto [write_error, written] = co_await boost::capy::write(
            secure, boost::capy::const_buffer(response.data(), response.size()));
        if (write_error || written != response.size()) {
            error_ = write_error ? write_error.message() : "short response write";
        }
        socket_.close();
    }

    [[nodiscard]] auto request_complete() const -> bool {
        const auto header_end = request_.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            return false;
        }
        const auto marker = request_.find("Content-Length: ");
        if (marker == std::string::npos || marker > header_end) {
            return true;
        }
        const auto value_start = marker + std::string_view("Content-Length: ").size();
        const auto value_end = request_.find("\r\n", value_start);
        const auto length = std::stoul(request_.substr(value_start, value_end - value_start));
        return request_.size() >= header_end + 4 + length;
    }

    boost::corosio::io_context context_;
    boost::corosio::tcp_acceptor acceptor_;
    boost::corosio::tcp_socket socket_;
    std::string body_;
    bool disconnect_{};
    std::atomic<std::uint32_t> connections_{};
    std::string request_;
    std::string error_;
    std::string certificate_;
    std::string private_key_;
};

auto request() -> liquify::AiRequest {
    return {.model = "claude-opus-5",
            .max_tokens = 512,
            .system = "Use graph context.",
            .prompt = "Explain this.",
            .context = "[liquify-context-v1]\npath: src/a.cpp"};
}

auto complete(liquify::AnthropicProvider& provider, HttpsDouble& server) -> liquify::AiResult {
    auto mutex = std::mutex{};
    auto condition = std::condition_variable{};
    auto done = false;
    auto result = liquify::AiResult{};
    provider.complete(request(), {.complete = [&](liquify::AiResult completed) {
                          {
                              auto lock = std::lock_guard(mutex);
                              result = std::move(completed);
                              done = true;
                          }
                          condition.notify_one();
                      }});
    server.run();
    auto lock = std::unique_lock(mutex);
    condition.wait_for(lock, 5s, [&] { return done; });
    EXPECT_TRUE(done);
    return result;
}

TEST(AiProvider, StreamsMessagesApiDeltas) {
    auto body = std::string{};
    body += "event: content_block_delta\ndata: "
            "{\"type\":\"content_block_delta\",\"delta\":";
    body += "{\"type\":\"text_delta\",\"text\":\"graph \"}}\n\n";
    body += "event: content_block_delta\ndata: "
            "{\"type\":\"content_block_delta\",\"delta\":";
    body += "{\"type\":\"text_delta\",\"text\":\"answer\"}}\n\n";
    body += "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":";
    body += "{\"stop_reason\":\"end_turn\"}}\n\n";
    body += "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
    auto server = HttpsDouble(std::move(body), false);
    auto provider = liquify::AnthropicProvider("test-key", server.endpoint());

    const auto result = complete(provider, server);

    EXPECT_TRUE(server.error().empty()) << server.error();
    EXPECT_EQ(server.connections(), 1U);
    EXPECT_EQ(result.text, "graph answer");
    EXPECT_EQ(result.stop_reason, liquify::AiStopReason::end_turn);
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_NE(server.request().find("POST /v1/messages HTTP/1.1"), std::string::npos);
    EXPECT_NE(server.request().find("x-api-key: test-key"), std::string::npos);
    EXPECT_NE(server.request().find("anthropic-version: 2023-06-01"), std::string::npos);
    EXPECT_NE(server.request().find("\"model\":\"claude-opus-5\""), std::string::npos);
    EXPECT_NE(server.request().find("\"stream\":true"), std::string::npos);
    EXPECT_EQ(server.request().find("\"thinking\""), std::string::npos);
}

TEST(AiProvider, ReportsRefusalAsAStopReason) {
    auto body = std::string{};
    body += "event: content_block_delta\ndata: "
            "{\"type\":\"content_block_delta\",\"delta\":";
    body += "{\"type\":\"text_delta\",\"text\":\"I cannot help.\"}}\n\n";
    body += "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":";
    body += "{\"stop_reason\":\"refusal\"}}\n\n";
    body += "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
    auto server = HttpsDouble(std::move(body), false);
    auto provider = liquify::AnthropicProvider("test-key", server.endpoint());

    const auto result = complete(provider, server);

    EXPECT_EQ(result.stop_reason, liquify::AiStopReason::refusal);
    EXPECT_EQ(result.text, "I cannot help.");
    EXPECT_TRUE(result.error.empty()) << result.error;
}

TEST(AiProvider, ReportsMidStreamDisconnect) {
    auto body = std::string{};
    body += "event: content_block_delta\ndata: "
            "{\"type\":\"content_block_delta\",\"delta\":";
    body += "{\"type\":\"text_delta\",\"text\":\"partial\"}}\n\n";
    auto server = HttpsDouble(std::move(body), true);
    auto provider = liquify::AnthropicProvider("test-key", server.endpoint());

    const auto result = complete(provider, server);

    EXPECT_EQ(result.stop_reason, liquify::AiStopReason::error);
    EXPECT_NE(result.error.find("disconnected"), std::string::npos) << result.error;
}

TEST(Assistant, OffDoesNotConstructAProviderOrConnect) {
    auto constructed = std::atomic<std::uint32_t>{};
    auto server = HttpsDouble({}, false);
    auto config = liquify::AiConfig{};
    config.enabled = false;
    auto assistant = liquify::Assistant(config, true, [&] {
        constructed.fetch_add(1);
        return std::make_unique<liquify::AnthropicProvider>("unused", server.endpoint());
    });

    assistant.submit(liquify::AssistantAction::ask, "question", {}, {});
    server.count_connections_while_idle();

    EXPECT_EQ(constructed.load(), 0U);
    EXPECT_EQ(server.connections(), 0U);
    EXPECT_FALSE(assistant.snapshot().enabled);
    EXPECT_EQ(assistant.snapshot().status, "off");
}

TEST(AiConfig, DefaultsOffAndRepositoryOverridesGlobalSetting) {
    auto location = TemporaryDirectory("liquify-ai-config");
    auto created = liquid::runtime::block_on(liquid::repository::create(location.path()));
    ASSERT_TRUE(created) << created.error().message;
    const auto defaults =
        liquify::load_ai_config(location.path(), location.path() / "missing-global-config");
    ASSERT_TRUE(defaults) << defaults.error().message;
    EXPECT_FALSE(defaults->enabled);
    const auto global_path = location.path() / "global-config";
    {
        auto output = std::ofstream(global_path);
        output << "liquify settings {\n";
        output << "  ai { enabled: false model: \"test-model\" }\n";
        output << "}\n";
    }
    const auto initial = liquify::load_ai_config(location.path(), global_path);
    ASSERT_TRUE(initial) << initial.error().message;
    EXPECT_FALSE(initial->enabled);
    EXPECT_EQ(initial->model, "test-model");

    ASSERT_TRUE(liquify::set_repository_ai_enabled(location.path(), true));
    const auto overridden = liquify::load_ai_config(location.path(), global_path);
    ASSERT_TRUE(overridden) << overridden.error().message;
    EXPECT_TRUE(overridden->enabled);
    EXPECT_EQ(overridden->model, "test-model");
}

TEST(ContextBuilder, UsesInteractiveGraphFactsAndReportsOmissions) {
    auto state = TemporaryDirectory("liquify-context-graph");
    const auto path = std::filesystem::path("src/example.cpp");
    const auto source = std::string{"int helper(int value);\nint target(int "
                                    "value) { return helper(value); }\n"};
    auto analysis = liquify::InteractiveAnalysis::open(state.path());
    ASSERT_TRUE(analysis) << analysis.error().message;
    ASSERT_TRUE(analysis->open_buffer(path, liquify::StubLanguage::cpp, source));
    auto helper = liquify::SymbolRecord{};
    helper.name = "helper";
    helper.definition = liquify::SymbolLocation{"include/example.hpp",
                                                {liquify::DeclarationKind::function,
                                                 "helper",
                                                 {},
                                                 "int helper(int value)",
                                                 {0, 21, 0, 0, 0, 21}}};
    auto target = liquify::SymbolRecord{};
    target.name = "target";
    target.definition = liquify::SymbolLocation{path,
                                                {liquify::DeclarationKind::function,
                                                 "target",
                                                 {},
                                                 "int target(int value)",
                                                 {23, 69, 1, 0, 1, 46}}};
    ASSERT_TRUE(analysis->set_symbol_records({helper, target}));
    const auto context = analysis->definition_context(path, 47);
    ASSERT_TRUE(context) << context.error().message;
    ASSERT_TRUE(context->enclosing_definition);
    ASSERT_TRUE(context->enclosing_symbol);
    ASSERT_GE(context->referenced_definitions.size(), 1U);

    auto facts = liquify::ContextFacts{};
    facts.path = path;
    facts.focus = context->enclosing_definition->range;
    facts.enclosing_definition = context->enclosing_definition;
    facts.enclosing_symbol = context->enclosing_symbol;
    for (const auto& reference : context->referenced_definitions) {
        facts.referenced_definitions.push_back(
            {reference.name,
             reference.signature,
             {reference.definition.path, reference.definition.declaration.range}});
    }
    facts.file_diagnostics.push_back(
        {{path, {47, 53, 1, 24, 1, 30}}, liquify::DiagnosticSeverity::warning, "graph diagnostic"});
    facts.build_result =
        liquify::ContextBuildResult{"compile-example", 1, "miss", "header digest changed"};
    for (auto index = 0; index < 20; ++index) {
        facts.dependency_edges.push_back(
            {"dependency-" + std::to_string(index), "compile-example"});
    }
    facts.whole_file_bytes = 12'000;

    const auto block = liquify::ContextBuilder(900).build(facts);

    EXPECT_LE(block.bytes, 900U);
    EXPECT_NE(block.text.find("target"), std::string::npos);
    EXPECT_NE(block.text.find("helper"), std::string::npos);
    EXPECT_NE(block.text.find("graph diagnostic"), std::string::npos);
    EXPECT_NE(block.text.find("header digest changed"), std::string::npos);
    EXPECT_FALSE(block.omissions.empty());
    EXPECT_NE(block.text.find("[omissions]"), std::string::npos);
}

TEST(Assistant, AppliesAProposedEditOnlyAfterAcceptanceAsOneDrip) {
    auto location = TemporaryDirectory("liquify-assisted-edit");
    auto created = liquid::runtime::block_on(liquid::repository::create(location.path()));
    ASSERT_TRUE(created) << created.error().message;
    auto repository = std::move(*created);
    auto written = liquid::runtime::block_on(repository.write("answer.txt", bytes("before")));
    ASSERT_TRUE(written) << written.error().message;
    auto buffer = liquify::EditorBuffer::open(location.path(), "answer.txt", "before-id");
    ASSERT_TRUE(buffer.error().empty()) << buffer.error();
    auto config = liquify::AiConfig{};
    config.enabled = true;
    auto assistant = liquify::Assistant(
        config, true, [] { return std::make_unique<ImmediateProvider>("after"); });

    assistant.submit(liquify::AssistantAction::propose_edit,
                     "replace it",
                     {.text = "[liquify-context-v1]"},
                     {0, 6, 0, 0, 0, 6});

    ASSERT_TRUE(assistant.snapshot().proposal);
    EXPECT_EQ(buffer.text(), "before");
    EXPECT_EQ(buffer.pending_drips(), 0U);
    assistant.accept();
    ASSERT_TRUE(assistant.snapshot().accepted);
    const auto accepted = assistant.take_accepted();
    ASSERT_TRUE(accepted);
    const auto edit = *accepted;
    ASSERT_TRUE(buffer.apply_edit(edit));
    ASSERT_TRUE(buffer.save()) << buffer.error();
    ASSERT_TRUE(liquify::record_assisted_edit(location.path(), "answer.txt", edit));

    EXPECT_EQ(buffer.text(), "after");
    EXPECT_EQ(buffer.pending_drips(), 1U);
    const auto log_path = location.path() / ".liquid" / "config.liquify.ai.log";
    auto log = std::ifstream(log_path);
    const auto contents = std::string(std::istreambuf_iterator<char>(log), {});
    EXPECT_NE(contents.find("\"assisted\":true"), std::string::npos);
    EXPECT_EQ(contents.find("replace it"), std::string::npos);
}

} // namespace
