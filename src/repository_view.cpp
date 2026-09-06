#include "repository_view.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <ranges>
#include <tuple>
#include <utility>

#include "liquid/drive.hpp"
#include "liquid/frontend.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

namespace liquify {
namespace {

std::mutex repository_worker_mutex;

auto error_message(const liquid::error& error) -> std::string {
    if (error.context.empty()) {
        return error.message;
    }
    return error.context + ": " + error.message;
}

auto symbol_kind(liquid::symbol_kind kind) -> NavigationSymbolKind {
    switch (kind) {
    case liquid::symbol_kind::function:
        return NavigationSymbolKind::function;
    case liquid::symbol_kind::type:
        return NavigationSymbolKind::type;
    case liquid::symbol_kind::variable:
        return NavigationSymbolKind::variable;
    case liquid::symbol_kind::field:
        return NavigationSymbolKind::field;
    case liquid::symbol_kind::parameter:
        return NavigationSymbolKind::parameter;
    case liquid::symbol_kind::namespace_:
        return NavigationSymbolKind::namespace_;
    case liquid::symbol_kind::unknown:
        return NavigationSymbolKind::unknown;
    }
    return NavigationSymbolKind::unknown;
}

auto navigation_range(const liquid::frontend_source_range& range) -> NavigationRange {
    return {range.start.byte,
            range.end.byte,
            range.start.line,
            range.start.column,
            range.end.line,
            range.end.column};
}

auto drive_state(liquid::drive_path_state state) -> NavigationDriveState {
    switch (state) {
    case liquid::drive_path_state::online_only:
        return NavigationDriveState::online_only;
    case liquid::drive_path_state::available_offline:
        return NavigationDriveState::available_offline;
    case liquid::drive_path_state::not_synced_here:
        return NavigationDriveState::not_synced_here;
    }
    return NavigationDriveState::unavailable;
}

auto path_matches(std::string_view changed, std::string_view path) -> bool {
    return changed == path || (changed.size() > path.size() && changed.starts_with(path) &&
                               changed[path.size()] == '/');
}

auto change_state(std::string_view path,
                  std::span<const std::string> modified,
                  std::span<const std::string> in_change,
                  std::span<const std::string> conflicted) -> NavigationChangeState {
    const auto contains = [path](const auto& values) {
        return std::ranges::any_of(values,
                                   [path](const auto& value) { return path_matches(value, path); });
    };
    if (contains(conflicted)) {
        return NavigationChangeState::conflicted;
    }
    if (contains(modified)) {
        return NavigationChangeState::modified;
    }
    if (contains(in_change)) {
        return NavigationChangeState::in_change;
    }
    return NavigationChangeState::clean;
}

struct RepositoryAnnotations {
    std::vector<std::string> modified;
    std::vector<std::string> in_change;
    std::vector<std::string> conflicted;
};

auto load_annotations(liquid::repository& repository) -> RepositoryAnnotations {
    auto output = RepositoryAnnotations{};
    const auto status = repository.status();
    if (status && status->head) {
        auto recorded = liquid::runtime::block_on(repository.at(*status->head));
        auto current = liquid::runtime::block_on(repository.root());
        if (recorded && current) {
            if (auto changes = liquid::runtime::block_on(
                    repository.diff(std::move(recorded->root), std::move(*current)))) {
                for (const auto& change : *changes) {
                    output.modified.push_back(change.path);
                }
            }
        }
    }
    if (status) {
        for (const auto& conflict : status->conflicts) {
            output.conflicted.push_back(conflict.path);
        }
    }
    if (auto log = liquid::runtime::block_on(repository.log({.first_parent = true}));
        log && !log->empty() && !log->front().parents.empty()) {
        if (auto changes = liquid::runtime::block_on(
                repository.diff(log->front().parents.front().id, log->front().id))) {
            for (const auto& change : *changes) {
                output.in_change.push_back(change.path);
            }
        }
    }
    return output;
}

template <class Output>
auto append_tree(liquid::repository& repository,
                 const liquid::tree& tree,
                 std::string_view directory,
                 std::span<const std::string> modified,
                 std::span<const std::string> in_change,
                 std::span<const std::string> conflicted,
                 const std::map<std::string, NavigationDriveState, std::less<>>& drive,
                 Output& output) -> liquid::result<void> {
    for (const auto& entry : tree.entries) {
        const auto path =
            directory.empty() ? entry.name : std::string(directory) + "/" + entry.name;
        const auto drive_position = drive.find(path);
        const auto path_drive = drive_position == drive.end() ? NavigationDriveState::unavailable
                                                              : drive_position->second;
        output.paths.push_back(
            {path,
             navigation_file_kind(path, entry.kind == liquid::node_kind::tree),
             path_drive,
             change_state(path, modified, in_change, conflicted),
             entry.kind == liquid::node_kind::blob ? liquid::to_string(entry.id) : std::string{}});
        if (entry.kind == liquid::node_kind::tree) {
            auto child = liquid::runtime::block_on(repository.tree(entry.ref));
            if (!child) {
                return std::unexpected(child.error());
            }
            auto appended = append_tree(repository,
                                        *child,
                                        path,
                                        modified,
                                        in_change,
                                        conflicted,
                                        drive,
                                        output);
            if (!appended) {
                return appended;
            }
            continue;
        }
        if (entry.kind != liquid::node_kind::blob) {
            continue;
        }
        auto symbols = liquid::runtime::block_on(repository.symbols(entry.id));
        if (!symbols) {
            if (symbols.error().code != liquid::errc::not_found && output.error.empty()) {
                output.error = error_message(symbols.error());
            }
            continue;
        }
        for (const auto& occurrence : symbols->occurrences) {
            output.symbols.push_back({occurrence.symbol,
                                      occurrence.name,
                                      symbol_kind(occurrence.kind),
                                      path,
                                      navigation_range(occurrence.range),
                                      occurrence.container,
                                      occurrence.role == liquid::symbol_role::definition});
        }
    }
    return {};
}

} // namespace

RepositoryView RepositoryView::open(std::filesystem::path path) {
    try {
        auto worker = std::async(std::launch::async, &RepositoryView::load, std::move(path));
        return worker.get();
    } catch (const std::exception& error) {
        RepositoryView view;
        view.error_ = error.what();
        return view;
    } catch (...) {
        RepositoryView view;
        view.error_ = "repository worker failed";
        return view;
    }
}

auto RepositoryView::entries() const -> std::span<const liquid::tree_entry> {
    return entries_;
}

auto RepositoryView::entries(std::string_view directory) const
    -> std::span<const liquid::tree_entry> {
    const auto found = directories_.find(directory);
    if (found == directories_.end() || !found->second.loaded) {
        return {};
    }
    return found->second.entries;
}

auto RepositoryView::error() const -> std::string_view {
    return error_;
}

auto RepositoryView::directory_error(std::string_view directory) const -> std::string_view {
    const auto found = directories_.find(directory);
    if (found == directories_.end()) {
        return {};
    }
    return found->second.error;
}

auto RepositoryView::directory_loading(std::string_view directory) const -> bool {
    const auto found = directories_.find(directory);
    return found != directories_.end() && found->second.pending.has_value();
}

auto RepositoryView::directory_loaded(std::string_view directory) const -> bool {
    const auto found = directories_.find(directory);
    return found != directories_.end() && found->second.loaded;
}

auto RepositoryView::loading_directory_count() const -> std::size_t {
    return std::ranges::count_if(directories_,
                                 [](const auto& item) { return item.second.pending.has_value(); });
}

auto RepositoryView::loaded_directory_count() const -> std::size_t {
    return std::ranges::count_if(directories_, [](const auto& item) { return item.second.loaded; });
}

auto RepositoryView::directory_error_count() const -> std::size_t {
    return std::ranges::count_if(directories_,
                                 [](const auto& item) { return !item.second.error.empty(); });
}

auto RepositoryView::open_time() const -> std::chrono::duration<double, std::milli> {
    return open_time_;
}

auto RepositoryView::navigation_paths() const -> std::span<const NavigationPath> {
    return navigation_paths_;
}

auto RepositoryView::navigation_symbols() const -> std::span<const NavigationSymbol> {
    return navigation_symbols_;
}

auto RepositoryView::navigation_loading() const -> bool {
    return navigation_pending_.has_value();
}

auto RepositoryView::navigation_error() const -> std::string_view {
    return navigation_error_;
}

auto RepositoryView::metadata(std::string_view path) const -> const NavigationPath* {
    const auto found = std::ranges::find(navigation_paths_, path, [](const auto& value) {
        return value.path.generic_string();
    });
    return found == navigation_paths_.end() ? nullptr : &*found;
}

auto RepositoryView::mutation_running() const -> bool {
    return mutation_pending_.has_value();
}

auto RepositoryView::mutation_status() const -> std::string_view {
    return mutation_status_;
}

void RepositoryView::request_directory(std::string path, liquid::digest id) {
    auto [position, inserted] = directories_.try_emplace(path);
    if (!inserted) {
        return;
    }
    position->second.pending.emplace(std::async(std::launch::async,
                                                &RepositoryView::load_directory,
                                                repository_path_,
                                                std::move(id)));
}

void RepositoryView::request_move(std::string from, std::string to) {
    if (mutation_pending_) {
        return;
    }
    mutation_status_ = "moving " + from;
    mutation_pending_.emplace(std::async(std::launch::async,
                                         &RepositoryView::move_path,
                                         repository_path_,
                                         std::move(from),
                                         std::move(to)));
}

void RepositoryView::request_new_file(std::string path) {
    if (mutation_pending_) {
        return;
    }
    mutation_status_ = "creating " + path;
    mutation_pending_.emplace(std::async(std::launch::async,
                                         &RepositoryView::create_file,
                                         repository_path_,
                                         std::move(path)));
}

void RepositoryView::poll() {
    for (auto& [path, state] : directories_) {
        static_cast<void>(path);
        if (!state.pending || state.pending->wait_for(std::chrono::milliseconds::zero()) !=
                                  std::future_status::ready) {
            continue;
        }
        try {
            auto loaded = state.pending->get();
            state.entries = std::move(loaded.entries);
            state.error = std::move(loaded.error);
        } catch (const std::exception& caught) {
            state.error = caught.what();
        } catch (...) {
            state.error = "repository worker failed";
        }
        state.pending.reset();
        state.loaded = true;
    }
    if (navigation_pending_ && navigation_pending_->wait_for(std::chrono::milliseconds::zero()) ==
                                   std::future_status::ready) {
        auto loaded = navigation_pending_->get();
        navigation_pending_.reset();
        if (navigation_reload_pending_) {
            navigation_reload_pending_ = false;
            start_navigation_load();
        } else {
            navigation_paths_ = std::move(loaded.paths);
            navigation_symbols_ = std::move(loaded.symbols);
            navigation_error_ = std::move(loaded.error);
        }
    }
    if (mutation_pending_ && mutation_pending_->wait_for(std::chrono::milliseconds::zero()) ==
                                 std::future_status::ready) {
        auto loaded = mutation_pending_->get();
        mutation_pending_.reset();
        mutation_status_ = std::move(loaded.status);
        if (!loaded.error.empty()) {
            mutation_status_ = std::move(loaded.error);
            return;
        }
        entries_ = std::move(loaded.entries);
        modified_paths_ = std::move(loaded.modified);
        in_change_paths_ = std::move(loaded.in_change);
        conflicted_paths_ = std::move(loaded.conflicted);
        directories_.clear();
        navigation_paths_.clear();
        navigation_symbols_.clear();
        navigation_error_.clear();
        if (navigation_pending_) {
            navigation_reload_pending_ = true;
        } else {
            start_navigation_load();
        }
    }
}

RepositoryView RepositoryView::load(std::filesystem::path path) {
    const auto lock = std::scoped_lock(repository_worker_mutex);
    RepositoryView view;
    view.repository_path_ = path;
    const auto started = std::chrono::steady_clock::now();
    auto opened = liquid::runtime::block_on(liquid::repository::open(std::move(path)));
    if (!opened) {
        view.error_ = error_message(opened.error());
        view.open_time_ = std::chrono::steady_clock::now() - started;
        return view;
    }

    auto root = liquid::runtime::block_on(opened->root());
    if (!root) {
        view.error_ = error_message(root.error());
        view.open_time_ = std::chrono::steady_clock::now() - started;
        return view;
    }

    view.entries_ = std::move(root->entries);
    auto annotations = load_annotations(*opened);
    view.modified_paths_ = std::move(annotations.modified);
    view.in_change_paths_ = std::move(annotations.in_change);
    view.conflicted_paths_ = std::move(annotations.conflicted);
    view.open_time_ = std::chrono::steady_clock::now() - started;
    view.start_navigation_load();
    return view;
}

auto RepositoryView::load_directory(std::filesystem::path repository_path, liquid::digest id)
    -> DirectoryLoad {
    const auto lock = std::scoped_lock(repository_worker_mutex);
    auto output = DirectoryLoad{};
    auto opened = liquid::runtime::block_on(liquid::repository::open(std::move(repository_path)));
    if (!opened) {
        output.error = error_message(opened.error());
        return output;
    }
    auto tree = liquid::runtime::block_on(opened->tree(std::move(id)));
    if (!tree) {
        output.error = error_message(tree.error());
        return output;
    }
    output.entries = std::move(tree->entries);
    return output;
}

auto RepositoryView::load_navigation(std::filesystem::path repository_path,
                                     std::vector<std::string> modified,
                                     std::vector<std::string> in_change,
                                     std::vector<std::string> conflicted) -> NavigationLoad {
    const auto lock = std::scoped_lock(repository_worker_mutex);
    auto output = NavigationLoad{};
    auto opened = liquid::runtime::block_on(liquid::repository::open(repository_path));
    if (!opened) {
        output.error = error_message(opened.error());
        return output;
    }
    auto root = liquid::runtime::block_on(opened->root());
    if (!root) {
        output.error = error_message(root.error());
        return output;
    }
    auto drive = std::map<std::string, NavigationDriveState, std::less<>>{};
    if (auto client = liquid::runtime::block_on(liquid::drive_client::open(repository_path))) {
        if (auto status = liquid::runtime::block_on(client->status())) {
            for (const auto& path : status->paths) {
                drive.emplace(path.path, drive_state(path.state));
            }
        }
    }
    auto appended = append_tree(*opened, *root, {}, modified, in_change, conflicted, drive, output);
    if (!appended && output.error.empty()) {
        output.error = error_message(appended.error());
    }
    std::ranges::sort(output.paths, {}, [](const auto& value) {
        return value.path.generic_string();
    });
    std::ranges::sort(output.symbols, [](const auto& left, const auto& right) {
        return std::tuple{left.name,
                          left.path.generic_string(),
                          left.range.start_line,
                          left.range.start_column} < std::tuple{right.name,
                                                                right.path.generic_string(),
                                                                right.range.start_line,
                                                                right.range.start_column};
    });
    return output;
}

auto RepositoryView::move_path(std::filesystem::path repository_path,
                               std::string from,
                               std::string to) -> MutationLoad {
    const auto lock = std::scoped_lock(repository_worker_mutex);
    auto output = MutationLoad{};
    auto opened = liquid::runtime::block_on(liquid::repository::open(std::move(repository_path)));
    if (!opened) {
        output.error = error_message(opened.error());
        return output;
    }
    auto moved = liquid::runtime::block_on(opened->move(from, to));
    if (!moved) {
        output.error = error_message(moved.error());
        return output;
    }
    auto root = liquid::runtime::block_on(opened->root());
    if (!root) {
        output.error = error_message(root.error());
        return output;
    }
    output.entries = std::move(root->entries);
    auto annotations = load_annotations(*opened);
    output.modified = std::move(annotations.modified);
    output.in_change = std::move(annotations.in_change);
    output.conflicted = std::move(annotations.conflicted);
    output.status = "moved " + from + " to " + to;
    return output;
}

auto RepositoryView::create_file(std::filesystem::path repository_path, std::string path)
    -> MutationLoad {
    const auto lock = std::scoped_lock(repository_worker_mutex);
    auto output = MutationLoad{};
    auto opened = liquid::runtime::block_on(liquid::repository::open(std::move(repository_path)));
    if (!opened) {
        output.error = error_message(opened.error());
        return output;
    }
    auto written = liquid::runtime::block_on(opened->write(path, {}));
    if (!written) {
        output.error = error_message(written.error());
        return output;
    }
    auto root = liquid::runtime::block_on(opened->root());
    if (!root) {
        output.error = error_message(root.error());
        return output;
    }
    output.entries = std::move(root->entries);
    auto annotations = load_annotations(*opened);
    output.modified = std::move(annotations.modified);
    output.in_change = std::move(annotations.in_change);
    output.conflicted = std::move(annotations.conflicted);
    output.status = "created " + path;
    return output;
}

void RepositoryView::start_navigation_load() {
    navigation_pending_.emplace(std::async(std::launch::async,
                                           &RepositoryView::load_navigation,
                                           repository_path_,
                                           modified_paths_,
                                           in_change_paths_,
                                           conflicted_paths_));
}

auto node_kind_name(liquid::node_kind kind) -> std::string_view {
    switch (kind) {
    case liquid::node_kind::blob:
        return "blob";
    case liquid::node_kind::tree:
        return "tree";
    case liquid::node_kind::change:
        return "change";
    case liquid::node_kind::action:
        return "action";
    case liquid::node_kind::result_:
        return "result";
    case liquid::node_kind::attestation:
        return "attestation";
    case liquid::node_kind::repo:
        return "repository";
    }
    return "unknown";
}

} // namespace liquify
