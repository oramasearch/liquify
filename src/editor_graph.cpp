#include "liquify/editor_graph.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <ranges>
#include <set>
#include <span>
#include <thread>
#include <utility>

#include "liquid/build.hpp"
#include "liquid/drive.hpp"
#include "liquid/frontend.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquid/sync.hpp"
#include "liquify/editor_model.hpp"
#include "liquify/first_run.hpp"
#include "liquify/lake_remote.hpp"
#include "liquify/plugin.hpp"
#include "liquify/stub_tree.hpp"

namespace liquify {
namespace {

// FNV-1a 64: a stable, short name for a repository's cache directory (not a security boundary; the
// directory is the user's own).
auto stable_name(std::string_view text) -> std::string {
  auto hash = std::uint64_t{0xcbf29ce484222325ULL};
  for (const auto character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 0x100000001b3ULL;
  }
  return std::format("{:016x}", hash);
}

auto error_message(const liquid::error &error) -> std::string {
  if (error.context.empty()) {
    return error.message;
  }
  return error.context + ": " + error.message;
}

auto short_digest(const liquid::digest &value) -> std::string {
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

auto language_name(StubLanguage language) -> std::string_view {
    return language == StubLanguage::c ? "c" : "c++";
}

auto source_bytes(std::string_view source) -> std::span<const std::byte> {
    return std::as_bytes(std::span(source.data(), source.size()));
}

auto action_identity(const std::optional<liquid::digest> &action,
                     std::uint64_t sequence) -> std::string {
  return action ? short_digest(*action) : "action " + std::to_string(sequence);
}

auto build_diagnostic_severity(liquid::frontend_diagnostic_severity severity)
    -> DiagnosticSeverity {
  if (severity == liquid::frontend_diagnostic_severity::note) {
    return DiagnosticSeverity::note;
  }
  if (severity == liquid::frontend_diagnostic_severity::warning) {
    return DiagnosticSeverity::warning;
  }
  return DiagnosticSeverity::error;
}

auto bearer_token(const liquid::remote_config &remote) -> std::string {
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

auto source_range(const liquid::frontend_source_range &range)
    -> std::optional<SourceRange> {
  const auto limit = std::numeric_limits<std::uint32_t>::max();
  if (range.start.byte > limit || range.end.byte > limit ||
      range.start.line > limit || range.end.line > limit ||
      range.start.column > limit || range.end.column > limit) {
    return std::nullopt;
  }
  return SourceRange{
      static_cast<std::uint32_t>(range.start.byte),
      static_cast<std::uint32_t>(range.end.byte),
      static_cast<std::uint32_t>(range.start.line == 0 ? 0
                                                       : range.start.line - 1),
      static_cast<std::uint32_t>(
          range.start.column == 0 ? 0 : range.start.column - 1),
      static_cast<std::uint32_t>(range.end.line == 0 ? 0 : range.end.line - 1),
      static_cast<std::uint32_t>(range.end.column == 0 ? 0
                                                       : range.end.column - 1)};
}

auto declaration_kind(liquid::symbol_kind kind) -> DeclarationKind {
  switch (kind) {
  case liquid::symbol_kind::function:
    return DeclarationKind::function;
  case liquid::symbol_kind::type:
    return DeclarationKind::type;
  case liquid::symbol_kind::field:
    return DeclarationKind::field;
  case liquid::symbol_kind::namespace_:
    return DeclarationKind::namespace_;
  case liquid::symbol_kind::variable:
  case liquid::symbol_kind::parameter:
  case liquid::symbol_kind::unknown:
    return DeclarationKind::variable;
  }
  return DeclarationKind::variable;
}

auto split_path(const std::filesystem::path &path) -> std::vector<std::string> {
  auto output = std::vector<std::string>{};
  for (const auto &component : path.lexically_normal()) {
    const auto value = component.generic_string();
    if (!value.empty() && value != ".") {
      output.push_back(value);
    }
  }
  return output;
}

auto find_blob(liquid::repository &repository, liquid::tree tree,
               std::span<const std::string> components, std::size_t index)
    -> liquid::result<std::optional<liquid::digest>> {
  if (index >= components.size()) {
    return std::optional<liquid::digest>{};
  }
  const auto found = std::ranges::find(tree.entries, components[index],
                                       &liquid::tree_entry::name);
  if (found == tree.entries.end()) {
    return std::optional<liquid::digest>{};
  }
  if (index + 1 == components.size()) {
    if (found->kind != liquid::node_kind::blob) {
      return std::optional<liquid::digest>{};
    }
    return found->id;
  }
  if (found->kind != liquid::node_kind::tree) {
    return std::optional<liquid::digest>{};
  }
  auto child = liquid::runtime::block_on(repository.tree(found->id));
  if (!child) {
    return std::unexpected(child.error());
  }
  return find_blob(repository, std::move(*child), components, index + 1);
}

auto signature_for(std::string_view name, const StubTree &stubs)
    -> const StubDeclaration * {
  const auto found =
      std::ranges::find(stubs.declarations, name, &StubDeclaration::name);
  return found == stubs.declarations.end() ? nullptr : &*found;
}

auto editor_symbols(const std::filesystem::path &path,
                    const liquid::symbol_record &record, const StubTree &stubs)
    -> std::vector<SymbolRecord> {
  auto records = std::map<std::string, SymbolRecord>{};
  for (const auto &occurrence : record.occurrences) {
    const auto range = source_range(occurrence.range);
    if (!range) {
      continue;
    }
    auto &symbol = records[occurrence.symbol];
    symbol.name = occurrence.name;
    if (occurrence.role == liquid::symbol_role::definition) {
      auto declaration = StubDeclaration{};
      if (const auto *stub = signature_for(occurrence.name, stubs);
          stub != nullptr) {
        declaration = *stub;
      } else {
        declaration.kind = declaration_kind(occurrence.kind);
        declaration.name = occurrence.name;
        declaration.signature = occurrence.name;
        declaration.range = *range;
      }
      declaration.range = *range;
      if (occurrence.documentation) {
        declaration.documentation = *occurrence.documentation;
      }
      symbol.definition = SymbolLocation{path, std::move(declaration)};
    } else {
      symbol.references.push_back(SourceLocation{path, *range});
    }
  }
  auto output = std::vector<SymbolRecord>{};
  output.reserve(records.size());
  for (auto &[id, record_value] : records) {
    static_cast<void>(id);
    if (!record_value.definition.declaration.name.empty()) {
      output.push_back(std::move(record_value));
    }
  }
  return output;
}

auto load_editor_symbols(const std::filesystem::path &repository_path,
                         const std::filesystem::path &path,
                         const StubTree &stubs)
    -> liquid::result<std::vector<SymbolRecord>> {
  auto opened =
      liquid::runtime::block_on(liquid::repository::open(repository_path));
  if (!opened) {
    return std::unexpected(opened.error());
  }
  auto root = liquid::runtime::block_on(opened->root());
  if (!root) {
    return std::unexpected(root.error());
  }
  const auto components = split_path(path);
  auto blob = find_blob(*opened, std::move(*root), components, 0);
  if (!blob) {
    return std::unexpected(blob.error());
  }
  if (!*blob) {
    return std::vector<SymbolRecord>{};
  }
  auto symbols = liquid::runtime::block_on(opened->symbols(**blob));
  if (!symbols) {
    if (symbols.error().code == liquid::errc::not_found) {
      return std::vector<SymbolRecord>{};
    }
    return std::unexpected(symbols.error());
  }
  return editor_symbols(path, *symbols, stubs);
}

auto cache_status_name(liquid::cache_status status) -> std::string {
  switch (status) {
  case liquid::cache_status::miss:
    return "miss";
  case liquid::cache_status::hit:
    return "hit";
  case liquid::cache_status::divergent:
    return "divergent";
  }
  return "unknown";
}

auto cache_miss_name(liquid::cache_miss_reason reason) -> std::string_view {
  switch (reason) {
  case liquid::cache_miss_reason::input_changed:
    return "input changed";
  case liquid::cache_miss_reason::command_changed:
    return "command changed";
  case liquid::cache_miss_reason::environment_changed:
    return "environment changed";
  case liquid::cache_miss_reason::toolchain_changed:
    return "toolchain changed";
  case liquid::cache_miss_reason::sandbox_changed:
    return "sandbox changed";
  case liquid::cache_miss_reason::never_built:
    return "never built";
  case liquid::cache_miss_reason::divergent_result_quarantined:
    return "divergent result quarantined";
  case liquid::cache_miss_reason::policy_refused:
    return "policy refused";
  case liquid::cache_miss_reason::non_hermetic_read:
    return "non-hermetic read";
  case liquid::cache_miss_reason::unknown:
    return "unknown";
  }
  return "unknown";
}

auto why_text(const std::optional<liquid::cache_miss> &why) -> std::string {
  if (!why) {
    return "cache hit";
  }
  auto output = std::string(cache_miss_name(why->reason));
  if (!why->path.empty()) {
    output += ": " + why->path;
  }
  if (!why->detail.empty()) {
    output += ": " + why->detail;
  }
  return output;
}

auto command_mentions(const liquid::action_input &input,
                      const std::filesystem::path &path) -> bool {
  const auto target = path.generic_string();
  return std::ranges::any_of(input.command, [&target](const auto &argument) {
    return argument == target || argument.ends_with("/" + target);
  });
}

void append_build_facts(const std::filesystem::path &repository_path,
                        const std::filesystem::path &path,
                        ContextFacts &facts) {
  auto opened =
      liquid::runtime::block_on(liquid::repository::open(repository_path));
  if (!opened) {
    return;
  }
  auto plan = liquid::runtime::block_on(opened->build_plan());
  if (!plan) {
    return;
  }
  const auto action =
      std::ranges::find_if(*plan, [&path](const auto &candidate) {
        return command_mentions(candidate.input, path);
      });
  if (action == plan->end()) {
    return;
  }
  if (action->result) {
    auto result = liquid::runtime::block_on(
        liquid::build::result(*opened, *action->result));
    if (result) {
      facts.build_result = ContextBuildResult{
          liquid::to_string(action->action), result->exit,
          cache_status_name(result->cache), why_text(result->why)};
    }
  }
  const auto action_name = liquid::to_string(action->action);
  for (const auto &dependency : action->dependencies) {
    facts.dependency_edges.push_back(
        {liquid::to_string(dependency), action_name});
  }
  for (const auto &candidate : *plan) {
    const auto depends_on_action = std::ranges::any_of(
        candidate.dependencies, [&action_name](const auto &dependency) {
          return liquid::to_string(dependency) == action_name;
        });
    if (depends_on_action) {
      facts.dependency_edges.push_back(
          {action_name, liquid::to_string(candidate.action)});
    }
  }
}

} // namespace

struct EditorGraph::Impl {
  struct Task {
    std::function<void()> run;
    bool concurrent{};
  };

  explicit Impl(std::filesystem::path path,
                std::shared_ptr<PluginHost> configured_plugins,
                std::filesystem::path configured_syntax_frontend)
      : repository_path(std::move(path)), plugins(std::move(configured_plugins)),
        syntax_frontend_path(std::move(configured_syntax_frontend)), worker([this] { run(); }) {}

  ~Impl() {
    wait_until_idle();
    {
      auto lock = std::lock_guard(mutex);
      stopping = true;
    }
    ready.notify_all();
    worker.join();
    // Every concurrent task's thread is joined before any member it uses is destroyed
    // (liquify#80): a detached thread could still be destroying its task after the idle count
    // reached zero.
    for (auto &running : concurrent) {
      running.thread.join();
    }
  }

  struct ConcurrentTask {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };

  // Joins the concurrent threads that have finished; only the worker thread calls this.
  void reap_concurrent() {
    std::erase_if(concurrent, [](ConcurrentTask &running) {
      if (!running.done->load()) {
        return false;
      }
      running.thread.join();
      return true;
    });
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
        reap_concurrent();
        auto done = std::make_shared<std::atomic<bool>>(false);
        concurrent.push_back(
            {std::thread([this, done, task = std::move(task)]() mutable {
               task.run();
               task = Task{};  // its captures are released before the task is reported finished
               finish_task();
               done->store(true);
             }),
             done});
      } else {
        task.run();
        finish_task();
      }
    }
    stop_hosted_daemon();
  }

  void initialize() {
    initialize_daemon();
    // The cache lives outside the repository (liquify#79).
    const auto cache = analysis_cache_directory(repository_path);
    {
      auto created = std::error_code{};
      std::filesystem::create_directories(cache, created);
    }
    auto opened = InteractiveAnalysis::open(cache / "liquify-graph", plugins);
    auto opened_stubs = StubTrees::open(cache / "liquify-stub-graph");
    auto loaded_frontend = liquid::result<liquid::frontend_module>{std::unexpected(
        liquid::error{liquid::errc::not_found, "tree-sitter frontend is unavailable"})};
    if (!syntax_frontend_path.empty()) {
        loaded_frontend = liquid::frontend_module::load(syntax_frontend_path);
    }
    auto lock = std::lock_guard(mutex);
    if (!opened) {
      current.error = error_message(opened.error());
    } else if (!opened_stubs) {
      current.error = error_message(opened_stubs.error());
    } else {
      analysis = std::make_shared<InteractiveAnalysis>(std::move(*opened));
      structural =
          std::make_unique<StructuralEditor>(repository_path, *analysis);
      stubs.emplace(std::move(*opened_stubs));
      current.ready = true;
      current.graph = analysis->statistics();
    }
    if (loaded_frontend) {
        syntax_frontend.emplace(std::move(*loaded_frontend));
    } else {
        current.syntax_error = error_message(loaded_frontend.error());
    }
    initialized = true;
    idle.notify_all();
  }

  void initialize_daemon() {
    auto options = liquid::daemon_options{};
    options.capture.exclude.push_back(".liquid/**");
    auto started = liquid::runtime::block_on(
        liquid::daemon::start(repository_path, options));
    if (started) {
      daemon.emplace(std::move(*started));
      daemon_hosted = true;
    } else if (started.error().code == liquid::errc::already_exists) {
      auto attached =
          liquid::runtime::block_on(liquid::daemon::attach(repository_path));
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

  void set_daemon_error(const liquid::error &error) {
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

  void record_query_error(const liquid::error &error) {
    auto lock = std::lock_guard(mutex);
    current.query_error = error_message(error);
    current.query_failure = error;
  }

  // Build and push failures belong to the Build panel, never to Diagnostics.
  void record_build_error(const liquid::error &error) {
    auto lock = std::lock_guard(mutex);
    current.build_error = error;
    current.build_running = false;
  }

  void record_deploy_error(const liquid::error &error) {
    auto lock = std::lock_guard(mutex);
    current.deploy.running = false;
    current.deploy.error = error_message(error);
    current.deploy.failure = error;
  }

  void clear_query_error() {
    auto lock = std::lock_guard(mutex);
    current.query_error.clear();
    current.query_failure.reset();
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

  void reset_build() {
    auto lock = std::lock_guard(mutex);
    build_requested_at = std::chrono::steady_clock::now();
    current.build_running = true;
    current.build_error.reset();
    current.build_result.clear();
    current.build_first_action = {};
    current.build_finished = {};
    current.build_actions.clear();
    current.critical_path.clear();
    current.test_results.clear();
    current.build_diagnostics.clear();
  }

  void set_build_plan(std::vector<liquid::build_plan_action> plan) {
    auto lock = std::lock_guard(mutex);
    current.build_actions.clear();
    current.build_actions.reserve(plan.size());
    for (const auto &action : plan) {
      auto snapshot = BuildActionSnapshot{
          short_digest(action.action), action.state == liquid::cache_status::hit
                                           ? BuildActionState::hit
                                           : BuildActionState::queued};
      snapshot.dependencies.reserve(action.dependencies.size());
      for (const auto &dependency : action.dependencies) {
        snapshot.dependencies.push_back(short_digest(dependency));
      }
      current.build_actions.push_back(std::move(snapshot));
    }
  }

  void apply_build_event(const liquid::build_event &event) {
    auto lock = std::lock_guard(mutex);
    auto find_action = [&]() -> BuildActionSnapshot * {
      if (event.action) {
        const auto identity = action_identity(event.action, event.sequence);
        const auto found = std::ranges::find(current.build_actions, identity,
                                             &BuildActionSnapshot::identity);
        if (found != current.build_actions.end()) {
          return &*found;
        }
      }
      const auto found = std::ranges::find_if(
          current.build_actions, [](const BuildActionSnapshot &action) {
            return action.state == BuildActionState::queued ||
                   action.state == BuildActionState::running_local ||
                   action.state == BuildActionState::running_remote;
          });
      return found == current.build_actions.end() ? nullptr : &*found;
    };
    if (event.kind == liquid::build_event_kind::action_queued) {
      current.build_actions.push_back(
          {action_identity(event.action, event.sequence),
           BuildActionState::queued});
      return;
    }
    if (event.kind == liquid::build_event_kind::action_started) {
      if (current.build_first_action == std::chrono::milliseconds{}) {
        current.build_first_action =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - build_requested_at);
      }
      if (auto *action = find_action()) {
        if (event.action) {
          action->identity = action_identity(event.action, event.sequence);
        }
        if (event.execution) {
          action->state =
              event.execution->kind == liquid::action_execution_kind::remote
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
      if (auto *action = find_action()) {
        if (event.action) {
          action->identity = action_identity(event.action, event.sequence);
        }
        action->state = event.kind == liquid::build_event_kind::action_hit
                            ? BuildActionState::hit
                        : event.kind == liquid::build_event_kind::action_done
                            ? BuildActionState::done
                            : BuildActionState::failed;
        for (const auto &diagnostic : event.diagnostics) {
          const auto line = diagnostic.line == 0 ? 0U : diagnostic.line - 1U;
          const auto column =
              diagnostic.column == 0 ? 0U : diagnostic.column - 1U;
          const auto path = diagnostic.file.empty()
                                ? active_buffer_path
                                : std::filesystem::path{diagnostic.file};
          auto editor_diagnostic =
              Diagnostic{{path,
                          {0, 0, static_cast<std::uint32_t>(line),
                           static_cast<std::uint32_t>(column),
                           static_cast<std::uint32_t>(line),
                           static_cast<std::uint32_t>(column)}},
                         build_diagnostic_severity(diagnostic.severity),
                         diagnostic.message};
          action->diagnostics.push_back(editor_diagnostic);
          current.build_diagnostics.push_back(std::move(editor_diagnostic));
        }
      }
      return;
    }
    if (event.kind == liquid::build_event_kind::critical_path_updated) {
      current.critical_path = event.critical_path;
      for (const auto &critical : event.critical_path) {
        const auto found = std::ranges::find(current.build_actions,
                                             short_digest(critical.action),
                                             &BuildActionSnapshot::identity);
        if (found != current.build_actions.end()) {
          found->critical = true;
          if (critical.schedule) {
            found->scheduling_choice = std::format(
                "{} (local {} ms, remote {} ms)", critical.schedule->reason,
                critical.schedule->local_estimate.count(),
                critical.schedule->remote_estimate.count());
          }
        }
      }
      return;
    }
    if (event.kind == liquid::build_event_kind::test_result && event.test) {
      const auto found = std::ranges::find(
          current.test_results, event.test->name, &TestSnapshot::name);
      auto result = TestSnapshot{
          event.test->name, event.test->status == liquid::test_status::passed,
          event.test->hit, event.test->output, event.test->duration};
      if (found == current.test_results.end()) {
        current.test_results.push_back(std::move(result));
      } else {
        *found = std::move(result);
      }
      return;
    }
    if (event.kind == liquid::build_event_kind::build_finished &&
        event.summary) {
      current.build_running = false;
      current.build_finished =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - build_requested_at);
      current.build_result = std::format(
          "{}: actions={} hits={} failures={}",
          event.summary->exit == 0 ? "passed" : "failed",
          event.summary->actions, event.summary->hits, event.summary->failures);
    }
  }

  void consume_build_events(liquid::build_handle handle) {
    auto events = handle.events();
    if (!events) {
      record_build_error(events.error());
      return;
    }
    while (true) {
      auto event = liquid::runtime::block_on(events->next());
      if (!event) {
        record_build_error(event.error());
        return;
      }
      if (!*event) {
        return;
      }
      apply_build_event(**event);
    }
  }

  void start_repository_build(liquid::build_target target) {
    if (!daemon) {
      record_build_error(
          {liquid::errc::not_found, "build daemon is unavailable", "editor"});
      return;
    }
    auto plan = liquid::runtime::block_on(daemon->build_plan(target));
    if (!plan) {
      record_build_error(plan.error());
      return;
    }
    set_build_plan(std::move(*plan));
    auto handle = liquid::runtime::block_on(daemon->build(std::move(target)));
    if (!handle) {
      record_build_error(handle.error());
      return;
    }
    consume_build_events(std::move(*handle));
  }

  void run_tests(std::string filter) {
    auto source = liquid::runtime::block_on(
        liquid::repository::open_build(repository_path));
    if (!source) {
      record_build_error(source.error());
      return;
    }
    auto targets = liquid::runtime::block_on(source->tests());
    if (!targets) {
      record_build_error(targets.error());
      return;
    }
    {
      auto lock = std::lock_guard(mutex);
      current.tests = std::move(*targets);
    }
    auto handle = source->run_tests(std::move(filter));
    if (!handle) {
      record_build_error(handle.error());
      return;
    }
    consume_build_events(std::move(*handle));
  }

  void load_tests() {
    // Listing tests in the background is not an operation the person asked for: a repository
    // with no recorded build simply has no tests yet, and other failures wait for F7 or F8.
    auto source = liquid::runtime::block_on(
        liquid::repository::open_build(repository_path));
    if (!source) {
      return;
    }
    auto targets = liquid::runtime::block_on(source->tests());
    if (!targets) {
      return;
    }
    auto lock = std::lock_guard(mutex);
    current.tests = std::move(*targets);
  }

  void refresh_editor_view(const std::filesystem::path& path,
                           StubLanguage language,
                           const std::string& source,
                           std::uint64_t request) {
      auto spans = std::vector<SyntaxSpan>{};
      auto folds = std::vector<FoldRange>{};
      auto syntax_error = std::string{};
      if (syntax_frontend) {
          auto tree = syntax_frontend->syntax(language_name(language), source_bytes(source));
          if (tree) {
              spans = syntax_spans(*tree, source);
              folds = fold_ranges(*tree);
          } else {
              syntax_error = error_message(tree.error());
          }
      } else {
          syntax_error = "tree-sitter frontend is unavailable";
      }

      auto attribution = std::vector<AttributionSnapshot>{};
      auto presence = std::vector<PresenceSnapshot>{};
      auto drive = std::optional<liquid::drive_path_status>{};
      auto opened = liquid::runtime::block_on(liquid::repository::open(repository_path));
      if (opened) {
          auto head = liquid::runtime::block_on(opened->head());
          if (head) {
              auto lines =
                  liquid::runtime::block_on(opened->attribution(path.generic_string(), *head));
              if (lines) {
                  attribution.reserve(lines->size());
                  for (const auto& line : *lines) {
                      attribution.push_back({static_cast<std::size_t>(line.line - 1),
                                             line.by.taap_id,
                                             short_digest(line.change.value),
                                             line.agent});
                  }
              }
          }
          auto active = liquid::runtime::block_on(opened->presence());
          if (active) {
              presence.reserve(active->size());
              for (const auto& value : *active) {
                  auto range = std::optional<SourceRange>{};
                  if (value.range) {
                      range = source_range(*value.range);
                  }
                  presence.push_back({value.actor.taap_id, value.path, range, value.activity});
              }
          }
      }
      auto drive_client = liquid::runtime::block_on(liquid::drive_client::open(repository_path));
      if (drive_client) {
          auto status = liquid::runtime::block_on(drive_client->path_status(path.generic_string()));
          if (status) {
              drive = std::move(*status);
          }
      }
      auto lock = std::lock_guard(mutex);
      if (request != editor_view_request) {
          return;
      }
      current.syntax = std::move(spans);
      current.folds = std::move(folds);
      current.syntax_error = std::move(syntax_error);
      current.attribution = std::move(attribution);
      current.presence = std::move(presence);
      current.drive = std::move(drive);
      current.syntax_generation = request;
      current.collaboration_generation = request;
  }

  void search_repository(std::string query, std::uint64_t request) {
      auto opened = liquid::runtime::block_on(liquid::repository::open(repository_path));
      if (!opened) {
          auto lock = std::lock_guard(mutex);
          current.search.error = error_message(opened.error());
          current.search_generation = request;
          return;
      }
      auto hits =
          liquid::runtime::block_on(opened->search(query, liquid::search_options{.limit = 500}));
      auto lock = std::lock_guard(mutex);
      if (request != search_request) {
          return;
      }
      current.search.query = std::move(query);
      current.search.hits.clear();
      current.search.error.clear();
      if (hits) {
          current.search.hits = std::move(*hits);
      } else {
          current.search.error = error_message(hits.error());
      }
      current.search_generation = request;
  }

  void replace_repository(std::string query,
                          std::string replacement,
                          bool regular_expression,
                          std::uint64_t request) {
      auto opened = liquid::runtime::block_on(liquid::repository::open(repository_path));
      if (!opened) {
          auto lock = std::lock_guard(mutex);
          current.search.error = error_message(opened.error());
          current.search_generation = request;
          return;
      }
      const auto liquid_query = regular_expression ? "/" + query + "/" : "\"" + query + "\"";
      auto hits = liquid::runtime::block_on(
          opened->search(liquid_query, liquid::search_options{.limit = 500}));
      if (!hits) {
          auto lock = std::lock_guard(mutex);
          current.search.error = error_message(hits.error());
          current.search_generation = request;
          return;
      }
      auto paths = std::set<std::string>{};
      for (const auto& hit : *hits) {
          paths.insert(hit.path);
      }
      auto replacements = std::uint64_t{};
      for (const auto& path : paths) {
          auto contents = liquid::runtime::block_on(opened->read(path));
          if (!contents) {
              continue;
          }
          const auto before =
              std::string(reinterpret_cast<const char*>(contents->data()), contents->size());
          const auto matches = find_text(before, query, {regular_expression, true});
          const auto after = replace_text(before, query, replacement, {regular_expression, true});
          if (before == after) {
              continue;
          }
          auto written = liquid::runtime::block_on(opened->write(
              path,
              std::vector<std::byte>(source_bytes(after).begin(), source_bytes(after).end())));
          if (!written) {
              auto lock = std::lock_guard(mutex);
              current.search.error = error_message(written.error());
              current.search_generation = request;
              return;
          }
          replacements += matches.size();
      }
      auto lock = std::lock_guard(mutex);
      current.search.query = std::move(query);
      current.search.hits = std::move(*hits);
      current.search.replacements = replacements;
      current.search.error.clear();
      current.search_generation = request;
      redo_operation.reset();
  }

  void undo_latest(std::uint64_t request) {
      auto opened = liquid::runtime::block_on(liquid::repository::open(repository_path));
      if (!opened) {
          record_history_error(opened.error(), request);
          return;
      }
      auto operations = liquid::runtime::block_on(opened->operations());
      if (!operations) {
          record_history_error(operations.error(), request);
          return;
      }
      for (const auto& operation : *operations) {
          if (operation.kind == "undo") {
              continue;
          }
          auto undone = liquid::runtime::block_on(opened->undo(operation.id));
          if (!undone) {
              if (undone.error().code == liquid::errc::unsupported) {
                  continue;
              }
              record_history_error(undone.error(), request);
              return;
          }
          auto refreshed = liquid::runtime::block_on(opened->operations());
          auto lock = std::lock_guard(mutex);
          if (refreshed && !refreshed->empty() && refreshed->front().kind == "undo") {
              redo_operation = refreshed->front().id;
          }
          current.history_error.clear();
          current.history_generation = request;
          return;
      }
      record_history_error({liquid::errc::not_found, "no undoable operation"}, request);
  }

  void redo_latest(std::uint64_t request) {
      if (!redo_operation) {
          record_history_error({liquid::errc::not_found, "no operation to redo"}, request);
          return;
      }
      auto opened = liquid::runtime::block_on(liquid::repository::open(repository_path));
      if (!opened) {
          record_history_error(opened.error(), request);
          return;
      }
      auto redone = liquid::runtime::block_on(opened->undo(*redo_operation));
      if (!redone) {
          record_history_error(redone.error(), request);
          return;
      }
      auto lock = std::lock_guard(mutex);
      redo_operation.reset();
      current.history_error.clear();
      current.history_generation = request;
  }

  void record_history_error(const liquid::error& error, std::uint64_t request) {
      auto lock = std::lock_guard(mutex);
      current.history_error = error_message(error);
      current.history_generation = request;
  }

  void deploy(std::string lake_remote) {
    {
      auto lock = std::lock_guard(mutex);
      current.deploy = {};
      current.deploy.running = true;
    }
    auto source =
        liquid::runtime::block_on(liquid::repository::open(repository_path));
    if (!source) {
      record_deploy_error(source.error());
      return;
    }
    // The push goes to the repository's configured sync remote, or else to the lake it was
    // cloned from (or that Set Lake recorded), which libliquid's drive client keeps.
    auto endpoint_url = std::string{};
    auto token = lake_token();
    if (const auto remote = source->remote_configuration()) {
      endpoint_url = remote->endpoint;
      token = bearer_token(*remote);
    } else {
      endpoint_url = std::move(lake_remote);
    }
    if (endpoint_url.empty()) {
      record_deploy_error({liquid::errc::not_configured,
                           "repository has no configured sync remote", ""});
      return;
    }
    {
      auto lock = std::lock_guard(mutex);
      current.deploy.remote = endpoint_url;
    }
    auto endpoint = liquid::sync_endpoint{
        endpoint_url,
        liquid::sync_endpoint_options{.bearer_token = std::move(token),
                                      .atomic = false}};
    auto pushed = liquid::runtime::block_on(endpoint.push(*source));
    auto lock = std::lock_guard(mutex);
    current.deploy.running = false;
    if (!pushed) {
      current.deploy.error = error_message(pushed.error());
      current.deploy.failure = pushed.error();
      return;
    }
    current.deploy.objects = pushed->sent.objects;
    current.deploy.bytes = pushed->sent.bytes;
    current.deploy.references = std::move(pushed->references);
  }

  std::filesystem::path repository_path;
  mutable std::mutex mutex;
  std::condition_variable ready;
  std::condition_variable idle;
  std::deque<Task> tasks;
  std::shared_ptr<InteractiveAnalysis> analysis;
  std::shared_ptr<PluginHost> plugins;
  std::filesystem::path syntax_frontend_path;
  std::optional<liquid::frontend_module> syntax_frontend;
  std::unique_ptr<StructuralEditor> structural;
  std::optional<StubTrees> stubs;
  std::optional<liquid::daemon> daemon;
  EditorGraphSnapshot current;
  std::filesystem::path active_buffer_path;
  std::thread worker;
  // Threads of concurrent tasks, joined (never detached): touched only by the worker thread, and by
  // the destructor after the worker has stopped.
  std::vector<ConcurrentTask> concurrent;
  std::uint64_t pending_tasks{};
  std::uint64_t diagnostics_request{};
  std::uint64_t hover_request{};
  std::uint64_t completion_request{};
  std::uint64_t definition_request{};
  std::uint64_t ai_context_request{};
  std::uint64_t structural_selection_request{};
  std::uint64_t structural_references_request{};
  std::uint64_t structural_preview_request{};
  std::uint64_t structural_result_request{};
  std::uint64_t editor_view_request{};
  std::uint64_t search_request{};
  std::uint64_t history_request{};
  std::optional<liquid::op_id> redo_operation;
  std::chrono::steady_clock::time_point last_edit{};
  std::chrono::steady_clock::time_point build_requested_at{};
  bool initialized{};
  bool stopping{};
  bool daemon_hosted{};
};

EditorGraph::EditorGraph(std::filesystem::path repository_path,
                         std::shared_ptr<PluginHost> plugins,
                         std::filesystem::path syntax_frontend)
    : impl_(std::make_unique<Impl>(
          std::move(repository_path), std::move(plugins), std::move(syntax_frontend))) {}

EditorGraph::~EditorGraph() = default;

void EditorGraph::open_buffer(std::filesystem::path path, StubLanguage language,
                              std::string source) {
  {
    auto lock = std::lock_guard(impl_->mutex);
    impl_->active_buffer_path = path;
    impl_->current.diagnostics.clear();
    impl_->current.hover.reset();
    impl_->current.completions.clear();
    impl_->current.definition.reset();
    impl_->current.ai_context.reset();
    impl_->current.structural_selection.reset();
    impl_->current.structural_references.clear();
    impl_->current.rename_preview.reset();
    impl_->current.extract_preview.reset();
    impl_->current.structural_result.reset();
    impl_->current.diagnostics_generation = ++impl_->diagnostics_request;
    impl_->current.hover_generation = ++impl_->hover_request;
    impl_->current.completion_generation = ++impl_->completion_request;
    impl_->current.definition_generation = ++impl_->definition_request;
    impl_->current.ai_context_generation = ++impl_->ai_context_request;
    impl_->current.structural_selection_generation =
        ++impl_->structural_selection_request;
    impl_->current.structural_references_generation =
        ++impl_->structural_references_request;
    impl_->current.structural_preview_generation =
        ++impl_->structural_preview_request;
    impl_->current.structural_result_generation =
        ++impl_->structural_result_request;
    impl_->last_edit = {};
  }
  impl_->enqueue({[implementation = impl_.get(), path = std::move(path),
                   language, source = std::move(source)]() mutable {
    if (!implementation->analysis || !implementation->stubs) {
      return;
    }
    auto stub = implementation->stubs->for_source(language, source);
    auto opened =
        implementation->structural->open_buffer(path, language, source);
    if (!opened) {
      implementation->record_query_error(opened.error());
      return;
    }
    if (!stub) {
      implementation->record_query_error(stub.error());
      return;
    }
    auto symbols =
        load_editor_symbols(implementation->repository_path, path, *stub);
    if (!symbols) {
      implementation->record_query_error(symbols.error());
      return;
    }
    auto stored =
        implementation->analysis->set_symbol_records(std::move(*symbols));
    if (!stored) {
      implementation->record_query_error(stored.error());
      return;
    }
    implementation->clear_query_error();
  }});
}

void EditorGraph::apply_edit(std::filesystem::path path, SourceEdit edit) {
  {
    auto lock = std::lock_guard(impl_->mutex);
    impl_->last_edit = std::chrono::steady_clock::now();
  }
  impl_->enqueue({[implementation = impl_.get(), path = std::move(path),
                   edit = std::move(edit)] {
    if (!implementation->analysis) {
      return;
    }
    auto applied = implementation->structural->apply_source_edit(path, edit);
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
    auto closed = implementation->structural->close_buffer(path);
    if (!closed) {
      implementation->record_query_error(closed.error());
    }
  }});
}

void EditorGraph::publish_build_diagnostics(
    std::filesystem::path buffer_path, std::filesystem::path translation_unit,
    std::vector<Diagnostic> diagnostics) {
  impl_->enqueue(
      {[implementation = impl_.get(), buffer_path = std::move(buffer_path),
        translation_unit = std::move(translation_unit),
        diagnostics = std::move(diagnostics)]() mutable {
        if (!implementation->analysis) {
          return;
        }
        auto published = implementation->analysis->set_build_diagnostics(
            std::move(buffer_path), std::move(translation_unit),
            std::move(diagnostics));
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
      {[implementation = impl_.get(), path = std::move(path), request,
        edit_started] {
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
               std::chrono::duration<double, std::milli>(completed -
                                                         edit_started)
                   .count();
         }
         implementation->current.query_error.clear();
       },
       true});
}

void EditorGraph::request_hover(std::filesystem::path path,
                                std::uint32_t byte) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->hover_request;
  }
  impl_->enqueue(
      {[implementation = impl_.get(), path = std::move(path), byte, request] {
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

void EditorGraph::request_completion(std::filesystem::path path,
                                     std::uint32_t byte) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->completion_request;
  }
  impl_->enqueue(
      {[implementation = impl_.get(), path = std::move(path), byte, request] {
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

void EditorGraph::request_definition(std::filesystem::path path,
                                     std::uint32_t byte) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->definition_request;
  }
  impl_->enqueue(
      {[implementation = impl_.get(), path = std::move(path), byte, request] {
         if (!implementation->analysis) {
           return;
         }
         auto definition =
             implementation->analysis->go_to_definition(path, byte);
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

void EditorGraph::request_editor_view(std::filesystem::path path,
                                      StubLanguage language,
                                      std::string source) {
    auto request = std::uint64_t{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->editor_view_request;
    }
    impl_->enqueue({[implementation = impl_.get(),
                     path = std::move(path),
                     language,
                     source = std::move(source),
                     request]() mutable {
        implementation->refresh_editor_view(path, language, source, request);
    }});
}

void EditorGraph::request_search(std::string query) {
    auto request = std::uint64_t{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->search_request;
        impl_->current.search.replacements = 0;
    }
    impl_->enqueue({[implementation = impl_.get(), query = std::move(query), request]() mutable {
        implementation->search_repository(std::move(query), request);
    }});
}

void EditorGraph::request_replace(std::string query,
                                  std::string replacement,
                                  bool regular_expression) {
    auto request = std::uint64_t{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->search_request;
    }
    impl_->enqueue({[implementation = impl_.get(),
                     query = std::move(query),
                     replacement = std::move(replacement),
                     regular_expression,
                     request]() mutable {
        implementation->replace_repository(
            std::move(query), std::move(replacement), regular_expression, request);
    }});
}

void EditorGraph::request_undo() {
    auto request = std::uint64_t{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->history_request;
    }
    impl_->enqueue(
        {[implementation = impl_.get(), request] { implementation->undo_latest(request); }});
}

void EditorGraph::request_redo() {
    auto request = std::uint64_t{};
    {
        auto lock = std::lock_guard(impl_->mutex);
        request = ++impl_->history_request;
    }
    impl_->enqueue(
        {[implementation = impl_.get(), request] { implementation->redo_latest(request); }});
}

void EditorGraph::request_structural_selection(
    std::filesystem::path path, std::uint32_t byte,
    std::optional<SourceRange> current, bool enclosing_definition) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->structural_selection_request;
  }
  impl_->enqueue({[implementation = impl_.get(), path = std::move(path), byte,
                   current, enclosing_definition, request] {
    if (!implementation->structural) {
      return;
    }
    auto chain = implementation->structural->selection_chain(path, byte);
    if (!chain) {
      implementation->record_query_error(chain.error());
      return;
    }
    auto selected = std::optional<SourceRange>{};
    if (enclosing_definition) {
      const auto definition =
          std::ranges::find_if(*chain, [](const auto &node) {
            return node.kind == "function_definition" ||
                   node.kind == "class_specifier" ||
                   node.kind == "struct_specifier" ||
                   node.kind == "enum_specifier" ||
                   node.kind == "namespace_definition";
          });
      if (definition != chain->end()) {
        selected = definition->range;
      }
    } else if (!current) {
      if (!chain->empty()) {
        selected = chain->front().range;
      }
    } else {
      const auto parent =
          std::ranges::find_if(*chain, [&current](const auto &node) {
            return node.range.start_byte <= current->start_byte &&
                   node.range.end_byte >= current->end_byte &&
                   node.range != *current;
          });
      if (parent != chain->end()) {
        selected = parent->range;
      }
    }
    auto lock = std::lock_guard(implementation->mutex);
    if (request == implementation->structural_selection_request) {
      implementation->current.structural_selection = selected;
      implementation->current.structural_selection_generation = request;
      implementation->current.query_error.clear();
    }
  }});
}

void EditorGraph::request_reference_selection(std::filesystem::path path,
                                              std::uint32_t byte) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->structural_references_request;
  }
  impl_->enqueue({[implementation = impl_.get(), path = std::move(path), byte,
                   request] {
    if (!implementation->structural) {
      return;
    }
    auto references = implementation->structural->select_references(path, byte);
    if (!references) {
      implementation->record_query_error(references.error());
      return;
    }
    auto lock = std::lock_guard(implementation->mutex);
    if (request == implementation->structural_references_request) {
      implementation->current.structural_references = std::move(*references);
      implementation->current.structural_references_generation = request;
      implementation->current.query_error.clear();
    }
  }});
}

void EditorGraph::request_rename(std::filesystem::path path, std::uint32_t byte,
                                 std::string new_name) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->structural_preview_request;
  }
  impl_->enqueue({[implementation = impl_.get(), path = std::move(path), byte,
                   new_name = std::move(new_name), request]() mutable {
    if (!implementation->structural) {
      return;
    }
    auto preview =
        implementation->structural->rename(path, byte, std::move(new_name));
    if (!preview) {
      implementation->record_query_error(preview.error());
      return;
    }
    auto lock = std::lock_guard(implementation->mutex);
    if (request == implementation->structural_preview_request) {
      implementation->current.rename_preview = std::move(*preview);
      implementation->current.extract_preview.reset();
      implementation->current.structural_preview_generation = request;
      implementation->current.query_error.clear();
    }
  }});
}

void EditorGraph::request_extract_function(std::filesystem::path path,
                                           SourceRange selection,
                                           std::string name) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->structural_preview_request;
  }
  impl_->enqueue({[implementation = impl_.get(), path = std::move(path),
                   selection, name = std::move(name), request]() mutable {
    if (!implementation->structural) {
      return;
    }
    auto preview = implementation->structural->extract_function(
        path, selection, std::move(name));
    if (!preview) {
      implementation->record_query_error(preview.error());
      return;
    }
    auto lock = std::lock_guard(implementation->mutex);
    if (request == implementation->structural_preview_request) {
      implementation->current.extract_preview = std::move(*preview);
      implementation->current.rename_preview.reset();
      implementation->current.structural_preview_generation = request;
      implementation->current.query_error.clear();
    }
  }});
}

void EditorGraph::apply_structural_edits(std::vector<StructuralEdit> edits) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->structural_result_request;
  }
  impl_->enqueue({[implementation = impl_.get(), edits = std::move(edits),
                   request]() mutable {
    if (!implementation->structural) {
      return;
    }
    auto result = implementation->structural->apply(edits);
    if (!result) {
      implementation->record_query_error(result.error());
      return;
    }
    auto lock = std::lock_guard(implementation->mutex);
    if (request == implementation->structural_result_request) {
      implementation->current.structural_result = std::move(*result);
      implementation->current.structural_result_generation = request;
      implementation->current.repository_pending_drips =
          implementation->structural->pending_drips();
      implementation->current.query_error.clear();
    }
  }});
}

void EditorGraph::request_ai_context(std::filesystem::path path,
                                     std::uint32_t byte,
                                     std::optional<SourceRange> selection,
                                     std::string selected_source,
                                     std::size_t whole_file_bytes,
                                     std::size_t byte_budget) {
  auto request = std::uint64_t{};
  {
    auto lock = std::lock_guard(impl_->mutex);
    request = ++impl_->ai_context_request;
  }
  impl_->enqueue(
      {[implementation = impl_.get(), path = std::move(path), byte, selection,
        selected_source = std::move(selected_source), whole_file_bytes,
        byte_budget, request]() mutable {
         if (!implementation->analysis) {
           return;
         }
         auto definition =
             implementation->analysis->definition_context(path, byte);
         if (!definition) {
           implementation->record_query_error(definition.error());
           return;
         }
         auto diagnostics = implementation->analysis->diagnostics(path);
         if (!diagnostics) {
           implementation->record_query_error(diagnostics.error());
           return;
         }
         auto facts = ContextFacts{};
         facts.path = path;
         facts.whole_file_bytes = whole_file_bytes;
         facts.enclosing_definition = definition->enclosing_definition;
         facts.enclosing_symbol = definition->enclosing_symbol;
         if (selection) {
           facts.focus = *selection;
           facts.selected_source = std::move(selected_source);
         } else if (definition->enclosing_definition) {
           facts.focus = definition->enclosing_definition->range;
           facts.selected_source = definition->enclosing_definition->spelling;
         } else {
           facts.focus = SourceRange{byte, byte, 0, 0, 0, 0};
         }
         for (auto &reference : definition->referenced_definitions) {
           facts.referenced_definitions.push_back(
               {std::move(reference.name), std::move(reference.signature),
                SourceLocation{reference.definition.path,
                               reference.definition.declaration.range}});
         }
         for (const auto &diagnostic : *diagnostics) {
           if (diagnostic.location.path == path) {
             facts.file_diagnostics.push_back(diagnostic);
           } else {
             facts.translation_unit_diagnostics.push_back(diagnostic);
           }
         }
         append_build_facts(implementation->repository_path, path, facts);
         auto context = ContextBuilder(byte_budget).build(facts);
         auto lock = std::lock_guard(implementation->mutex);
         if (request != implementation->ai_context_request) {
           return;
         }
         implementation->current.ai_context = std::move(context);
         implementation->current.ai_context_focus = facts.focus;
         implementation->current.ai_context_generation = request;
         implementation->current.query_error.clear();
       },
       true});
}

void EditorGraph::request_build(liquid::build_target target) {
  impl_->reset_build();
  impl_->enqueue(
      {[implementation = impl_.get(), target = std::move(target)]() mutable {
        implementation->start_repository_build(std::move(target));
      }});
}

void EditorGraph::request_tests(std::string filter) {
  impl_->reset_build();
  impl_->enqueue(
      {[implementation = impl_.get(), filter = std::move(filter)]() mutable {
        implementation->run_tests(std::move(filter));
      }});
}

void EditorGraph::refresh_tests() {
  impl_->enqueue(
      {[implementation = impl_.get()] { implementation->load_tests(); }});
}

void EditorGraph::request_push(std::string lake_remote) {
  impl_->enqueue({[implementation = impl_.get(),
                   lake_remote = std::move(lake_remote)]() mutable {
    implementation->deploy(std::move(lake_remote));
  }});
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
  impl_->redo_operation.reset();
}

void EditorGraph::refresh_status() {
  impl_->enqueue({[implementation = impl_.get()] {
    implementation->refresh_daemon_status();
  }});
}

auto EditorGraph::snapshot() const -> EditorGraphSnapshot {
  auto lock = std::lock_guard(impl_->mutex);
  return impl_->current;
}

void EditorGraph::wait_until_idle() { impl_->wait_until_idle(); }

auto editor_graph_status(const EditorGraphSnapshot &snapshot) -> std::string {
  if (!snapshot.ready) {
    return snapshot.error.empty() ? "analysis graph starting"
                                  : "analysis graph: " + snapshot.error;
  }
  auto output = std::format("repository drips pending={}",
                            snapshot.repository_pending_drips);
  if (snapshot.build_running) {
    const auto running = std::count_if(
        snapshot.build_actions.begin(), snapshot.build_actions.end(),
        [](const BuildActionSnapshot &action) {
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
      snapshot.daemon->targets.begin(), snapshot.daemon->targets.end(),
      std::uint64_t{}, [](std::uint64_t total, const auto &target) {
        return total + target.behind;
      });
  if (snapshot.daemon->last_drip) {
    output += " | drip " + short_digest(snapshot.daemon->last_drip->value) +
              " triggered ";
    if (snapshot.daemon->invalidated_paths.empty()) {
      output += "no paths";
    } else {
      for (std::size_t index = 0;
           index < snapshot.daemon->invalidated_paths.size(); ++index) {
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

auto analysis_cache_directory(const std::filesystem::path &repository_path,
                              std::optional<std::filesystem::path> state_root)
    -> std::filesystem::path {
  auto code = std::error_code{};
  auto canonical = std::filesystem::weakly_canonical(repository_path, code);
  const auto identity =
      (code ? std::filesystem::absolute(repository_path) : canonical).generic_string();
  const auto root = state_root.value_or(default_first_run_state_path().parent_path());
  return root / "analysis" / stable_name(identity);
}

} // namespace liquify
