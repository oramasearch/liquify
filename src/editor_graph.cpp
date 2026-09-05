#include "liquify/editor_graph.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <numeric>
#include <ranges>
#include <utility>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquid/sync.hpp"

namespace liquify {
namespace {

auto error_message(const liquid::error& error) -> std::string {
    if (error.context.empty()) {
        return error.message;
    }
    return error.context + ": " + error.message;
}

auto short_digest(const liquid::digest& value) -> std::string {
    auto text = liquid::to_string(value);
    const auto separator = text.find(':');
    if (separator != std::string::npos) {
        text = text.substr(separator + 1);
    }
    if (text.size() > 12) {
        text.resize(12);
    }
    return text;
}

auto action_identity(const std::optional<liquid::digest>& action, std::uint64_t sequence)
    -> std::string {
    return action ? short_digest(*action) : "action " + std::to_string(sequence);
}

auto diagnostic_severity(liquid::frontend_diagnostic_severity severity) -> DiagnosticSeverity {
    if (severity == liquid::frontend_diagnostic_severity::note) {
        return DiagnosticSeverity::note;
    }
    if (severity == liquid::frontend_diagnostic_severity::warning) {
        return DiagnosticSeverity::warning;
    }
    return DiagnosticSeverity::error;
}

auto bearer_token(const liquid::remote_config& remote) -> std::string {
    const auto authorization = remote.headers.find("authorization");
    if (authorization == remote.headers.end()) {
        return {};
    }
    constexpr auto prefix = std::string_view{"Bearer "};
    if (authorization->second.starts_with(prefix)) {
        return authorization->second.substr(prefix.size());
    }
    return authorization->second;
}

} // namespace

struct EditorGraph::Impl {
    struct Task {
        std::function<void()> run;
        bool concurrent{};
    };

    explicit Impl(std::filesystem::path path) : repository_path(std::move(path)) {
        start_async(initialize());
    }

    ~Impl() {
        wait_until_idle();
        if (daemon && daemon_hosted) {
            start_async(stop_hosted_daemon());
            wait_until_idle();
        }
    }

    [[nodiscard]] auto initialize() -> liquid::runtime::task<void> {
        auto options = liquid::daemon_options{};
        options.capture.exclude.push_back(".liquid/**");
        auto started = co_await liquid::daemon::start(repository_path, options);
        if (started) {
            daemon.emplace(std::move(*started));
            daemon_hosted = true;
        } else if (started.error().code == liquid::errc::already_exists) {
            auto attached = co_await liquid::daemon::attach(repository_path);
            if (attached) {
                daemon.emplace(std::move(*attached));
            } else {
                set_daemon_error(attached.error());
            }
        } else {
            set_daemon_error(started.error());
        }
        if (daemon) {
            auto status = co_await daemon->status();
            if (status) {
                auto lock = std::lock_guard(mutex);
                current.daemon_running = status->running;
                current.daemon_hosted = daemon_hosted;
                current.daemon = std::move(*status);
            } else {
                set_daemon_error(status.error());
            }
        }
        auto opened = co_await liquid::runtime::offload(
            [path = repository_path / ".liquid" / "liquify-graph"] {
                return InteractiveAnalysis::open(path);
            });
        auto lock = std::lock_guard(mutex);
        if (!opened) {
            current.error = error_message(opened.error());
        } else if (!*opened) {
            current.error = error_message(opened->error());
        } else {
            analysis = std::make_shared<InteractiveAnalysis>(std::move(**opened));
            current.ready = true;
            current.graph = analysis->statistics();
        }
        initialized = true;
        idle.notify_all();
        co_return {};
    }

    void set_daemon_error(const liquid::error& error) {
        auto lock = std::lock_guard(mutex);
        current.daemon_running = false;
        current.daemon_hosted = false;
        current.query_error = "daemon: " + error_message(error);
    }

    [[nodiscard]] auto stop_hosted_daemon() -> liquid::runtime::task<void> {
        if (!daemon || !daemon_hosted) {
            co_return {};
        }
        auto stopped = co_await daemon->shutdown();
        if (!stopped) {
            co_return std::unexpected(stopped.error());
        }
        auto waited = co_await daemon->wait();
        if (!waited) {
            co_return std::unexpected(waited.error());
        }
        co_return {};
    }

    void enqueue(Task task) {
        auto start_drain = false;
        {
            auto lock = std::lock_guard(mutex);
            ++pending_tasks;
            tasks.push_back(std::move(task));
            if (!draining) {
                draining = true;
                start_drain = true;
            }
        }
        if (start_drain) {
            liquid::runtime::spawn_serial(drain(), [this](liquid::result<void> completed) {
                if (!completed) {
                    record_query_error(completed.error());
                }
            });
        }
    }

    [[nodiscard]] auto drain() -> liquid::runtime::task<void> {
        while (true) {
            auto task = Task{};
            {
                auto lock = std::lock_guard(mutex);
                if (tasks.empty()) {
                    draining = false;
                    co_return {};
                }
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            while (!initialized) {
                auto [delay_error] = co_await liquid::runtime::delay(std::chrono::milliseconds{1});
                if (delay_error) {
                    co_return std::unexpected(
                        liquid::error{liquid::errc::internal, delay_error.message(), "editor graph"});
                }
            }
            auto completed = co_await liquid::runtime::offload([task = std::move(task)]() mutable {
                task.run();
            });
            if (!completed) {
                record_query_error(completed.error());
            }
            finish_task();
        }
    }

    void consume_async(liquid::build_handle handle) {
        start_async(consume_build_events(std::move(handle)));
    }

    void start_async(liquid::runtime::task<void> task) {
        {
            auto lock = std::lock_guard(mutex);
            ++pending_tasks;
        }
        liquid::runtime::spawn_serial(std::move(task), [this](liquid::result<void> completed) {
            if (!completed) {
                record_query_error(completed.error());
                auto lock = std::lock_guard(mutex);
                current.build_running = false;
            }
            finish_task();
        });
    }

    void finish_task() {
        auto lock = std::lock_guard(mutex);
        if (analysis) {
            current.graph = analysis->statistics();
        }
        --pending_tasks;
        if (pending_tasks == 0) {
            idle.notify_all();
        }
    }

    void wait_until_idle() {
        auto lock = std::unique_lock(mutex);
        idle.wait(lock, [this] { return initialized && pending_tasks == 0; });
    }

    void record_query_error(const liquid::error& error) {
        auto lock = std::lock_guard(mutex);
        current.query_error = error_message(error);
    }

    void clear_query_error() {
        auto lock = std::lock_guard(mutex);
        current.query_error.clear();
    }

    [[nodiscard]] auto refresh_daemon_status() -> liquid::runtime::task<void> {
        if (!daemon) {
            co_return {};
        }
        auto status = co_await daemon->status();
        auto lock = std::lock_guard(mutex);
        if (!status) {
            current.daemon_running = false;
            current.query_error = "daemon: " + error_message(status.error());
            co_return {};
        }
        current.daemon_running = status->running;
        current.daemon_hosted = daemon_hosted;
        current.daemon = std::move(*status);
        co_return {};
    }

    void reset_build() {
        auto lock = std::lock_guard(mutex);
        current.build_running = true;
        current.build_result.clear();
        current.build_actions.clear();
        current.critical_path.clear();
        current.test_results.clear();
        current.build_diagnostics.clear();
    }

    void set_build_plan(std::vector<liquid::build_plan_action> plan) {
        auto lock = std::lock_guard(mutex);
        current.build_actions.clear();
        current.build_actions.reserve(plan.size());
        for (const auto& action : plan) {
            current.build_actions.push_back(
                {short_digest(action.action),
                 action.state == liquid::cache_status::hit ? BuildActionState::hit
                                                           : BuildActionState::queued});
        }
    }

    void apply_build_event(const liquid::build_event& event) {
        auto lock = std::lock_guard(mutex);
        auto find_action = [&]() -> BuildActionSnapshot* {
            if (event.action) {
                const auto identity = action_identity(event.action, event.sequence);
                const auto found = std::ranges::find(
                    current.build_actions, identity, &BuildActionSnapshot::identity);
                if (found != current.build_actions.end()) {
                    return &*found;
                }
            }
            const auto found = std::ranges::find_if(
                current.build_actions, [](const BuildActionSnapshot& action) {
                    return action.state == BuildActionState::queued ||
                           action.state == BuildActionState::running_local ||
                           action.state == BuildActionState::running_remote;
                });
            return found == current.build_actions.end() ? nullptr : &*found;
        };
        if (event.kind == liquid::build_event_kind::action_queued) {
            current.build_actions.push_back(
                {action_identity(event.action, event.sequence), BuildActionState::queued});
            return;
        }
        if (event.kind == liquid::build_event_kind::action_started) {
            if (auto* action = find_action()) {
                if (event.action) {
                    action->identity = action_identity(event.action, event.sequence);
                }
                if (event.execution) {
                    action->state = event.execution->kind == liquid::action_execution_kind::remote
                                        ? BuildActionState::running_remote
                                        : BuildActionState::running_local;
                    action->worker = event.execution->worker;
                }
            }
            return;
        }
        if (event.kind == liquid::build_event_kind::action_hit ||
            event.kind == liquid::build_event_kind::action_done ||
            event.kind == liquid::build_event_kind::action_failed) {
            if (auto* action = find_action()) {
                if (event.action) {
                    action->identity = action_identity(event.action, event.sequence);
                }
                action->state = event.kind == liquid::build_event_kind::action_hit
                                    ? BuildActionState::hit
                                    : event.kind == liquid::build_event_kind::action_done
                                          ? BuildActionState::done
                                          : BuildActionState::failed;
                for (const auto& diagnostic : event.diagnostics) {
                    const auto line = diagnostic.line == 0 ? 0U : diagnostic.line - 1U;
                    const auto column = diagnostic.column == 0 ? 0U : diagnostic.column - 1U;
                    const auto path = diagnostic.file.empty() ? active_buffer_path
                                                              : std::filesystem::path{diagnostic.file};
                    auto editor_diagnostic = Diagnostic{
                        {path,
                         {0,
                          0,
                          static_cast<std::uint32_t>(line),
                          static_cast<std::uint32_t>(column),
                          static_cast<std::uint32_t>(line),
                          static_cast<std::uint32_t>(column)}},
                        diagnostic_severity(diagnostic.severity),
                        diagnostic.message};
                    action->diagnostics.push_back(editor_diagnostic);
                    current.build_diagnostics.push_back(std::move(editor_diagnostic));
                }
            }
            return;
        }
        if (event.kind == liquid::build_event_kind::critical_path_updated) {
            current.critical_path = event.critical_path;
            for (const auto& critical : event.critical_path) {
                const auto found = std::ranges::find(current.build_actions,
                                                     short_digest(critical.action),
                                                     &BuildActionSnapshot::identity);
                if (found != current.build_actions.end()) {
                    found->critical = true;
                    if (critical.schedule) {
                        found->scheduling_choice = critical.schedule->reason;
                    }
                }
            }
            return;
        }
        if (event.kind == liquid::build_event_kind::test_result && event.test) {
            const auto found = std::ranges::find(
                current.test_results, event.test->name, &TestSnapshot::name);
            auto result = TestSnapshot{event.test->name,
                                       event.test->status == liquid::test_status::passed,
                                       event.test->hit,
                                       event.test->output,
                                       event.test->duration};
            if (found == current.test_results.end()) {
                current.test_results.push_back(std::move(result));
            } else {
                *found = std::move(result);
            }
            return;
        }
        if (event.kind == liquid::build_event_kind::build_finished && event.summary) {
            current.build_running = false;
            current.build_result = std::format("{}: actions={} hits={} failures={}",
                                               event.summary->exit == 0 ? "passed" : "failed",
                                               event.summary->actions,
                                               event.summary->hits,
                                               event.summary->failures);
        }
    }

    [[nodiscard]] auto consume_build_events(liquid::build_handle handle)
        -> liquid::runtime::task<void> {
        auto events = handle.events();
        if (!events) {
            co_return std::unexpected(events.error());
        }
        while (true) {
            auto event = co_await events->next();
            if (!event) {
                co_return std::unexpected(event.error());
            }
            if (!*event) {
                break;
            }
            apply_build_event(**event);
        }
        co_return {};
    }

    [[nodiscard]] auto start_repository_build(liquid::build_target target)
        -> liquid::runtime::task<void> {
        if (!daemon) {
            co_return std::unexpected(
                liquid::error{liquid::errc::not_found, "build daemon is unavailable", "editor"});
        }
        auto plan = co_await daemon->build_plan(target);
        if (!plan) {
            co_return std::unexpected(plan.error());
        }
        set_build_plan(std::move(*plan));
        auto handle = co_await daemon->build(std::move(target));
        if (!handle) {
            co_return std::unexpected(handle.error());
        }
        co_return co_await consume_build_events(std::move(*handle));
    }

    [[nodiscard]] auto run_tests(std::string filter) -> liquid::runtime::task<void> {
        auto source = co_await liquid::repository::open_build(repository_path);
        if (!source) {
            co_return std::unexpected(source.error());
        }
        auto targets = co_await source->tests();
        if (!targets) {
            co_return std::unexpected(targets.error());
        }
        {
            auto lock = std::lock_guard(mutex);
            current.tests = std::move(*targets);
        }
        auto handle = source->run_tests(std::move(filter));
        if (!handle) {
            co_return std::unexpected(handle.error());
        }
        co_return co_await consume_build_events(std::move(*handle));
    }

    [[nodiscard]] auto load_tests() -> liquid::runtime::task<void> {
        auto source = co_await liquid::repository::open_build(repository_path);
        if (!source) {
            co_return std::unexpected(source.error());
        }
        auto targets = co_await source->tests();
        if (!targets) {
            co_return std::unexpected(targets.error());
        }
        auto lock = std::lock_guard(mutex);
        current.tests = std::move(*targets);
        co_return {};
    }

    [[nodiscard]] auto deploy() -> liquid::runtime::task<void> {
        {
            auto lock = std::lock_guard(mutex);
            current.deploy = {};
            current.deploy.running = true;
        }
        auto source = co_await liquid::repository::open(repository_path);
        if (!source) {
            co_return std::unexpected(source.error());
        }
        const auto remote = source->remote_configuration();
        if (!remote) {
            co_return std::unexpected(liquid::error{liquid::errc::not_found,
                                                     "repository has no configured sync remote",
                                                     repository_path.string()});
        }
        auto endpoint = liquid::sync_endpoint{
            remote->endpoint,
            liquid::sync_endpoint_options{.bearer_token = bearer_token(*remote), .atomic = false}};
        auto pushed = co_await endpoint.push(*source);
        auto lock = std::lock_guard(mutex);
        current.deploy.running = false;
        if (!pushed) {
            current.deploy.error = error_message(pushed.error());
            co_return {};
        }
        current.deploy.objects = pushed->sent.objects;
        current.deploy.bytes = pushed->sent.bytes;
        current.deploy.references = std::move(pushed->references);
        co_return {};
    }

    std::filesystem::path repository_path;
    mutable std::mutex mutex;
    std::condition_variable idle;
    std::deque<Task> tasks;
    std::shared_ptr<InteractiveAnalysis> analysis;
    std::optional<liquid::daemon> daemon;
    EditorGraphSnapshot current;
    std::filesystem::path active_buffer_path;
    std::uint64_t pending_tasks{};
    std::uint64_t diagnostics_request{};
    std::uint64_t hover_request{};
    std::uint64_t completion_request{};
    std::uint64_t definition_request{};
    std::chrono::steady_clock::time_point last_edit{};
    bool initialized{};
    bool daemon_hosted{};
    bool draining{};
};

EditorGraph::EditorGraph(std::filesystem::path repository_path)
    : impl_(std::make_unique<Impl>(std::move(repository_path))) {}

EditorGraph::~EditorGraph() = default;

void EditorGraph::open_buffer(std::filesystem::path path,
                              StubLanguage language,
                              std::string source) {
    {
        auto lock = std::lock_guard(impl_->mutex);
        impl_->active_buffer_path = path;
        impl_->current.diagnostics.clear();
        impl_->current.hover.reset();
        impl_->current.completions.clear();
        impl_->current.definition.reset();
        impl_->current.diagnostics_generation = ++impl_->diagnostics_request;
        impl_->current.hover_generation = ++impl_->hover_request;
        impl_->current.completion_generation = ++impl_->completion_request;
        impl_->current.definition_generation = ++impl_->definition_request;
        impl_->last_edit = {};
    }
    impl_->enqueue({[implementation = impl_.get(),
                     path = std::move(path),
                     language,
                     source = std::move(source)]() mutable {
        if (!implementation->analysis) {
            return;
        }
        auto opened = implementation->analysis->open_buffer(path, language, std::move(source));
        if (!opened) {
            implementation->record_query_error(opened.error());
        } else {
            implementation->clear_query_error();
        }
    }});
}

void EditorGraph::apply_edit(std::filesystem::path path, SourceEdit edit) {
    {
        auto lock = std::lock_guard(impl_->mutex);
        impl_->last_edit = std::chrono::steady_clock::now();
    }
    impl_->enqueue({[implementation = impl_.get(), path = std::move(path), edit = std::move(edit)] {
        if (!implementation->analysis) {
            return;
        }
        auto applied = implementation->analysis->apply_edit(path, edit);
        if (!applied) {
            implementation->record_query_error(applied.error());
        } else {
            implementation->clear_query_error();
        }
    }});
}

void EditorGraph::close_buffer(std::filesystem::path path) {
    impl_->enqueue({[implementation = impl_.get(), path = std::move(path)] {
        if (!implementation->analysis) {
            return;
        }
        auto closed = implementation->analysis->close_buffer(path);
        if (!closed) {
            implementation->record_query_error(closed.error());
        }
    }});
}

void EditorGraph::publish_build_diagnostics(std::filesystem::path buffer_path,
                                            std::filesystem::path translation_unit,
                                            std::vector<Diagnostic> diagnostics) {
    impl_->enqueue({[implementation = impl_.get(),
                     buffer_path = std::move(buffer_path),
                     translation_unit = std::move(translation_unit),
                     diagnostics = std::move(diagnostics)]() mutable {
        if (!implementation->analysis) {
            return;
        }
        auto published = implementation->analysis->set_build_diagnostics(
            std::move(buffer_path), std::move(translation_unit), std::move(diagnostics));
        if (!published) {
            implementation->record_query_error(published.error());
        }
    }});
}

void EditorGraph::request_diagnostics(std::filesystem::path path) {
    auto request = std::uint64_t{};
    auto edit_started = std::chrono::steady_clock::time_point{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->diagnostics_request;
        edit_started = impl_->last_edit;
    }
    impl_->enqueue(
        {[implementation = impl_.get(), path = std::move(path), request, edit_started] {
             if (!implementation->analysis) {
                 return;
             }
             auto diagnostics = implementation->analysis->diagnostics(path);
             if (!diagnostics) {
                 implementation->record_query_error(diagnostics.error());
                 return;
             }
             const auto completed = std::chrono::steady_clock::now();
             auto lock = std::lock_guard(implementation->mutex);
             if (request != implementation->diagnostics_request) {
                 return;
             }
             implementation->current.diagnostics = std::move(*diagnostics);
             implementation->current.diagnostics_generation = request;
             if (edit_started != std::chrono::steady_clock::time_point{}) {
                 implementation->current.edit_to_diagnostic_ms =
                     std::chrono::duration<double, std::milli>(completed - edit_started).count();
             }
             implementation->current.query_error.clear();
         },
         true});
}

void EditorGraph::request_hover(std::filesystem::path path, std::uint32_t byte) {
    auto request = std::uint64_t{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->hover_request;
    }
    impl_->enqueue({[implementation = impl_.get(), path = std::move(path), byte, request] {
                        if (!implementation->analysis) {
                            return;
                        }
                        auto hover = implementation->analysis->hover(path, byte);
                        if (!hover) {
                            implementation->record_query_error(hover.error());
                            return;
                        }
                        auto lock = std::lock_guard(implementation->mutex);
                        if (request == implementation->hover_request) {
                            implementation->current.hover = std::move(*hover);
                            implementation->current.hover_generation = request;
                            implementation->current.query_error.clear();
                        }
                    },
                    true});
}

void EditorGraph::request_completion(std::filesystem::path path, std::uint32_t byte) {
    auto request = std::uint64_t{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->completion_request;
    }
    impl_->enqueue({[implementation = impl_.get(), path = std::move(path), byte, request] {
                        if (!implementation->analysis) {
                            return;
                        }
                        auto completions = implementation->analysis->completion(path, byte);
                        if (!completions) {
                            implementation->record_query_error(completions.error());
                            return;
                        }
                        auto lock = std::lock_guard(implementation->mutex);
                        if (request == implementation->completion_request) {
                            implementation->current.completions = std::move(*completions);
                            implementation->current.completion_generation = request;
                            implementation->current.query_error.clear();
                        }
                    },
                    true});
}

void EditorGraph::request_definition(std::filesystem::path path, std::uint32_t byte) {
    auto request = std::uint64_t{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->definition_request;
    }
    impl_->enqueue({[implementation = impl_.get(), path = std::move(path), byte, request] {
                        if (!implementation->analysis) {
                            return;
                        }
                        auto definition = implementation->analysis->go_to_definition(path, byte);
                        if (!definition) {
                            implementation->record_query_error(definition.error());
                            return;
                        }
                        auto lock = std::lock_guard(implementation->mutex);
                        if (request == implementation->definition_request) {
                            implementation->current.definition = std::move(*definition);
                            implementation->current.definition_generation = request;
                            implementation->current.query_error.clear();
                        }
                    },
                    true});
}

void EditorGraph::request_build(liquid::build_target target) {
    impl_->reset_build();
    impl_->start_async(impl_->start_repository_build(std::move(target)));
}

void EditorGraph::request_tests(std::string filter) {
    impl_->reset_build();
    impl_->start_async(impl_->run_tests(std::move(filter)));
}

void EditorGraph::refresh_tests() {
    impl_->start_async(impl_->load_tests());
}

void EditorGraph::request_deploy() {
    impl_->start_async(impl_->deploy());
}

void EditorGraph::publish_build_event(liquid::build_event event) {
    impl_->apply_build_event(event);
}

void EditorGraph::publish_deploy_report(liquid::sync_report report) {
    auto lock = std::lock_guard(impl_->mutex);
    impl_->current.deploy.running = false;
    impl_->current.deploy.error.clear();
    impl_->current.deploy.objects = report.sent.objects;
    impl_->current.deploy.bytes = report.sent.bytes;
    impl_->current.deploy.references = std::move(report.references);
}

void EditorGraph::note_saved_drip(std::uint64_t repository_pending_drips) {
    auto lock = std::lock_guard(impl_->mutex);
    impl_->current.repository_pending_drips = repository_pending_drips;
}

void EditorGraph::refresh_status() {
    impl_->start_async(impl_->refresh_daemon_status());
}

auto EditorGraph::snapshot() const -> EditorGraphSnapshot {
    auto lock = std::lock_guard(impl_->mutex);
    return impl_->current;
}

void EditorGraph::wait_until_idle() {
    impl_->wait_until_idle();
}

auto editor_graph_status(const EditorGraphSnapshot& snapshot) -> std::string {
    if (!snapshot.ready) {
        return snapshot.error.empty() ? "analysis graph starting"
                                      : "analysis graph: " + snapshot.error;
    }
    auto output = std::format("repository drips pending={}", snapshot.repository_pending_drips);
    if (snapshot.build_running) {
        const auto running = std::count_if(
            snapshot.build_actions.begin(), snapshot.build_actions.end(), [](const auto& action) {
                return action.state == BuildActionState::queued ||
                       action.state == BuildActionState::running_local ||
                       action.state == BuildActionState::running_remote;
            });
        output += std::format(" | actions running={}", running);
    } else if (!snapshot.build_result.empty()) {
        output += " | last build " + snapshot.build_result;
    }
    if (!snapshot.daemon) {
        return output + " | daemon unavailable";
    }
    const auto backlog = std::accumulate(
        snapshot.daemon->targets.begin(),
        snapshot.daemon->targets.end(),
        std::uint64_t{},
        [](std::uint64_t total, const auto& target) { return total + target.behind; });
    if (snapshot.daemon->last_drip) {
        output += " | drip " + short_digest(snapshot.daemon->last_drip->value) + " triggered ";
        if (snapshot.daemon->invalidated_paths.empty()) {
            output += "no paths";
        } else {
            for (std::size_t index = 0; index < snapshot.daemon->invalidated_paths.size();
                 ++index) {
                if (index != 0) {
                    output += ", ";
                }
                output += snapshot.daemon->invalidated_paths[index];
            }
        }
    } else {
        output += " | waiting for first workspace drip";
    }
    output += std::format(" | validation backlog={}", backlog);
    return output;
}

} // namespace liquify
