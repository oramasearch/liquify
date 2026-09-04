#include "repository_view.hpp"

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

auto RepositoryView::error() const -> std::string_view {
    return error_;
}

RepositoryView RepositoryView::load(std::filesystem::path path) {
    RepositoryView view;
    auto opened = liquid::runtime::block_on(liquid::repository::open(std::move(path)));
    if (!opened) {
        view.error_ = error_message(opened.error());
        return view;
    }

    auto root = liquid::runtime::block_on(opened->root());
    if (!root) {
        view.error_ = error_message(root.error());
        return view;
    }

    view.entries_ = std::move(root->entries);
    return view;
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
