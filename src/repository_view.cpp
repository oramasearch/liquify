#include "repository_view.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <utility>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

namespace liquify {
namespace {

auto error_message(const liquid::error& error) -> std::string {
    if (error.context.empty()) {
        return error.message;
    }
    return error.context + ": " + error.message;
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

void RepositoryView::request_directory(std::string path, liquid::digest id) {
    auto [position, inserted] = directories_.try_emplace(path);
    if (!inserted) {
        return;
    }
    position->second.pending.emplace(std::async(
        std::launch::async, &RepositoryView::load_directory, repository_path_, std::move(id)));
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
}

RepositoryView RepositoryView::load(std::filesystem::path path) {
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
    view.open_time_ = std::chrono::steady_clock::now() - started;
    return view;
}

auto RepositoryView::load_directory(std::filesystem::path repository_path, liquid::digest id)
    -> DirectoryLoad {
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
