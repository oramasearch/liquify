#include "liquify/editor_graph.hpp"

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
#include <span>
#include <thread>
#include <utility>

#include "liquid/build.hpp"
#include "liquid/frontend.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/stub_tree.hpp"

namespace liquify {
namespace {

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
    auto opened = InteractiveAnalysis::open(repository_path / ".liquid" /
                                            "liquify-graph");
    auto opened_stubs =
        StubTrees::open(repository_path / ".liquid" / "liquify-stub-graph");
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
  std::unique_ptr<StructuralEditor> structural;
  std::optional<StubTrees> stubs;
  std::optional<liquid::daemon> daemon;
  EditorGraphSnapshot current;
  std::thread worker;
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
  std::chrono::steady_clock::time_point last_edit{};
  bool initialized{};
  bool stopping{};
  bool daemon_hosted{};
};

EditorGraph::EditorGraph(std::filesystem::path repository_path)
    : impl_(std::make_unique<Impl>(std::move(repository_path))) {}

EditorGraph::~EditorGraph() = default;

void EditorGraph::open_buffer(std::filesystem::path path, StubLanguage language,
                              std::string source) {
  {
    auto lock = std::lock_guard(impl_->mutex);
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

void EditorGraph::note_saved_drip(std::uint64_t repository_pending_drips) {
  auto lock = std::lock_guard(impl_->mutex);
  impl_->current.repository_pending_drips = repository_pending_drips;
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

} // namespace liquify
