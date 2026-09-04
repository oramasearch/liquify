#include "liquify/editor_graph.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <numeric>
#include <thread>
#include <utility>

#include "liquid/runtime.hpp"

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

} // namespace

struct EditorGraph::Impl {
    struct Task {
        std::function<void()> run;
        bool concurrent{};
    };

    explicit Impl(std::filesystem::path path)
        : repository_path(std::move(path)), worker([this] { run(); }) {}

    ~Impl() {
        wait_until_idle();
        {
            auto lock = std::lock_guard(mutex);
            stopping = true;
        }
        ready.notify_all();
        worker.join();
    }

    void run() {
        initialize();
        while (true) {
            auto task = Task{};
            {
                auto lock = std::unique_lock(mutex);
                ready.wait(lock, [this] { return stopping || !tasks.empty(); });
                if (stopping && tasks.empty()) {
                    break;
                }
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            if (task.concurrent) {
                std::thread([this, task = std::move(task)]() mutable {
                    task.run();
                    finish_task();
                }).detach();
            } else {
                task.run();
                finish_task();
            }
        }
        stop_hosted_daemon();
    }

    void initialize() {
        initialize_daemon();
        auto opened = InteractiveAnalysis::open(repository_path / ".liquid" / "liquify-graph");
        auto lock = std::lock_guard(mutex);
        if (!opened) {
            current.error = error_message(opened.error());
        } else {
            analysis = std::make_shared<InteractiveAnalysis>(std::move(*opened));
            current.ready = true;
            current.graph = analysis->statistics();
        }
        initialized = true;
        idle.notify_all();
    }

    void initialize_daemon() {
        auto options = liquid::daemon_options{};
        options.capture.exclude.push_back(".liquid/**");
        auto started = liquid::runtime::block_on(liquid::daemon::start(repository_path, options));
        if (started) {
            daemon.emplace(std::move(*started));
            daemon_hosted = true;
        } else if (started.error().code == liquid::errc::already_exists) {
            auto attached = liquid::runtime::block_on(liquid::daemon::attach(repository_path));
            if (attached) {
                daemon.emplace(std::move(*attached));
            } else {
                set_daemon_error(attached.error());
            }
        } else {
            set_daemon_error(started.error());
        }
        if (daemon) {
            refresh_daemon_status();
        }
    }

    void set_daemon_error(const liquid::error& error) {
        auto lock = std::lock_guard(mutex);
        current.daemon_running = false;
        current.daemon_hosted = false;
        current.query_error = "daemon: " + error_message(error);
    }

    void stop_hosted_daemon() {
        if (!daemon || !daemon_hosted) {
            return;
        }
        static_cast<void>(liquid::runtime::block_on(daemon->shutdown()));
        static_cast<void>(liquid::runtime::block_on(daemon->wait()));
    }

    void enqueue(Task task) {
        {
            auto lock = std::lock_guard(mutex);
            ++pending_tasks;
            tasks.push_back(std::move(task));
        }
        ready.notify_one();
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

    void refresh_daemon_status() {
        if (!daemon) {
            return;
        }
        auto status = liquid::runtime::block_on(daemon->status());
        auto lock = std::lock_guard(mutex);
        if (!status) {
            current.daemon_running = false;
            current.query_error = "daemon: " + error_message(status.error());
            return;
        }
        current.daemon_running = status->running;
        current.daemon_hosted = daemon_hosted;
        current.daemon = std::move(*status);
    }

    std::filesystem::path repository_path;
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable idle;
    std::deque<Task> tasks;
    std::shared_ptr<InteractiveAnalysis> analysis;
    std::optional<liquid::daemon> daemon;
    EditorGraphSnapshot current;
    std::thread worker;
    std::uint64_t pending_tasks{};
    std::uint64_t diagnostics_request{};
    std::uint64_t hover_request{};
    std::uint64_t completion_request{};
    std::uint64_t definition_request{};
    std::chrono::steady_clock::time_point last_edit{};
    bool initialized{};
    bool stopping{};
    bool daemon_hosted{};
};

EditorGraph::EditorGraph(std::filesystem::path repository_path)
    : impl_(std::make_unique<Impl>(std::move(repository_path))) {}

EditorGraph::~EditorGraph() = default;

void EditorGraph::open_buffer(std::filesystem::path path,
                              StubLanguage language,
                              std::string source) {
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

void EditorGraph::note_saved_drip(std::uint64_t repository_pending_drips) {
    auto lock = std::lock_guard(impl_->mutex);
    impl_->current.repository_pending_drips = repository_pending_drips;
}

void EditorGraph::refresh_status() {
    impl_->enqueue({[implementation = impl_.get()] { implementation->refresh_daemon_status(); }});
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
