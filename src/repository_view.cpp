#include "repository_view.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <future>
#include <utility>
#include <vector>

#include "liquid/drive.hpp"
#include "liquid/hash.hpp"
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

auto validate_clone_target(const std::filesystem::path& target) -> liquid::result<bool> {
    auto code = std::error_code{};
    const auto exists = std::filesystem::exists(target, code);
    if (code) {
        return std::unexpected(liquid::error{liquid::errc::io, code.message(), target.string()});
    }
    if (!exists) {
        return false;
    }
    if (std::filesystem::is_symlink(target, code)) {
        return std::unexpected(liquid::error{liquid::errc::already_exists,
                                             "clone target must not be a symbolic link",
                                             target.string()});
    }
    if (code) {
        return std::unexpected(liquid::error{liquid::errc::io, code.message(), target.string()});
    }
    if (!std::filesystem::is_directory(target, code)) {
        return std::unexpected(liquid::error{
            liquid::errc::already_exists, "clone target is not a directory", target.string()});
    }
    if (code) {
        return std::unexpected(liquid::error{liquid::errc::io, code.message(), target.string()});
    }
    if (!std::filesystem::is_empty(target, code)) {
        return std::unexpected(liquid::error{
            liquid::errc::already_exists, "clone target is not empty", target.string()});
    }
    if (code) {
        return std::unexpected(liquid::error{liquid::errc::io, code.message(), target.string()});
    }
    return true;
}

void clean_failed_clone(const std::filesystem::path& target, bool existed) {
    auto code = std::error_code{};
    if (!std::filesystem::is_directory(target, code) || code) {
        return;
    }
    auto entries = std::vector<std::filesystem::path>{};
    for (auto entry = std::filesystem::directory_iterator{target, code};
         !code && entry != std::filesystem::directory_iterator{};
         entry.increment(code)) {
        entries.push_back(entry->path());
    }
    for (const auto& entry : entries) {
        std::filesystem::remove_all(entry, code);
        if (code) {
            return;
        }
    }
    if (!existed) {
        std::filesystem::remove(target, code);
    }
}

auto is_offline_error(liquid::errc code) -> bool {
    return code == liquid::errc::io || code == liquid::errc::unavailable;
}

auto display_date(std::chrono::sys_seconds at) -> std::string {
    return std::format("{:%Y-%m-%d}", std::chrono::floor<std::chrono::days>(at));
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

RepositoryView
RepositoryView::clone(std::filesystem::path path, std::string remote, std::string bearer_token) {
    try {
        auto worker = std::async(std::launch::async,
                                 &RepositoryView::clone_impl,
                                 std::move(path),
                                 std::move(remote),
                                 std::move(bearer_token));
        return worker.get();
    } catch (const std::exception& error) {
        RepositoryView view;
        view.error_ = error.what();
        return view;
    } catch (...) {
        RepositoryView view;
        view.error_ = "repository clone worker failed";
        return view;
    }
}

auto RepositoryView::path() const -> const std::filesystem::path& {
    return repository_path_;
}

auto RepositoryView::name() const -> std::string {
    auto name = repository_path_.filename().string();
    return name.empty() ? repository_path_.string() : name;
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

auto RepositoryView::branch() const -> std::string_view {
    return branch_;
}

auto RepositoryView::current_change() const -> std::string_view {
    return current_change_;
}

auto RepositoryView::last_change() const -> std::string_view {
    return last_change_;
}

auto RepositoryView::last_changed_at() const -> std::string_view {
    return last_changed_at_;
}

auto RepositoryView::offline_reason() const -> std::string_view {
    return offline_reason_;
}

auto RepositoryView::offline() const -> bool {
    return offline_;
}

auto RepositoryView::remote_bytes() const -> std::uint64_t {
    return remote_bytes_;
}

auto RepositoryView::hydration_debt() const -> std::uint64_t {
    return hydration_debt_;
}

auto RepositoryView::pending_uploads() const -> std::uint64_t {
    return pending_uploads_;
}

auto RepositoryView::presence_count() const -> std::size_t {
    return presence_count_;
}

auto RepositoryView::clone_objects_received() const -> std::uint64_t {
    return clone_objects_received_;
}

auto RepositoryView::clone_bytes_received() const -> std::uint64_t {
    return clone_bytes_received_;
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
    auto opened = liquid::runtime::block_on(liquid::repository::open(path));
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
    const auto status = opened->status();
    if (status) {
        view.remote_bytes_ = status->remote_bytes;
        if (status->head) {
            view.current_change_ = liquid::to_string(status->head->value);
        }
    }
    const auto refs = liquid::runtime::block_on(opened->refs());
    if (refs && status && status->head) {
        for (const auto& reference : *refs) {
            if (liquid::to_string(reference.id.value) != view.current_change_) {
                continue;
            }
            constexpr auto branch_prefix = std::string_view{"refs/heads/"};
            view.branch_ = reference.name.starts_with(branch_prefix)
                               ? reference.name.substr(branch_prefix.size())
                               : reference.name;
            break;
        }
    }
    const auto log = liquid::runtime::block_on(opened->log());
    if (log && !log->empty()) {
        view.last_change_ = log->front().message;
        view.last_changed_at_ = display_date(log->front().at);
    }
    const auto presence = liquid::runtime::block_on(opened->presence());
    if (presence) {
        view.presence_count_ = presence->size();
    }
    auto drive = liquid::runtime::block_on(liquid::drive_client::open(path));
    if (drive) {
        const auto drive_status = liquid::runtime::block_on(drive->status());
        if (drive_status) {
            view.hydration_debt_ = drive_status->hydration_debt;
            view.pending_uploads_ = drive_status->pending_uploads;
            view.offline_reason_ = drive_status->offline_reason;
            view.offline_ = !view.offline_reason_.empty();
        }
    }
    view.open_time_ = std::chrono::steady_clock::now() - started;
    return view;
}

RepositoryView RepositoryView::clone_impl(std::filesystem::path path,
                                          std::string remote,
                                          std::string bearer_token) {
    RepositoryView view;
    view.repository_path_ = path;
    const auto valid = validate_clone_target(path);
    if (!valid) {
        view.error_ = error_message(valid.error());
        return view;
    }

    auto options = liquid::drive_options{};
    options.bearer_token = std::move(bearer_token);
    auto client = liquid::runtime::block_on(
        liquid::drive_client::open(path, std::move(remote), std::move(options)));
    if (!client) {
        view.error_ = error_message(client.error());
        view.offline_ = is_offline_error(client.error().code);
        clean_failed_clone(path, *valid);
        return view;
    }
    auto pinned = liquid::runtime::block_on(client->pin(""));
    if (!pinned) {
        const auto error = pinned.error();
        client = liquid::drive_client{};
        view.error_ = error_message(error);
        view.offline_ = is_offline_error(error.code);
        clean_failed_clone(path, *valid);
        return view;
    }
    auto report = liquid::runtime::block_on(client->sync());
    if (!report) {
        const auto error = report.error();
        client = liquid::drive_client{};
        view.error_ = error_message(error);
        view.offline_ = is_offline_error(error.code);
        clean_failed_clone(path, *valid);
        return view;
    }
    for (const auto& reference : report->status.references) {
        if (reference.outcome != liquid::sync_ref_outcome_kind::refused) {
            continue;
        }
        client = liquid::drive_client{};
        view.error_ = reference.name + ": " + reference.reason;
        clean_failed_clone(path, *valid);
        return view;
    }
    view.clone_objects_received_ = report->received.objects;
    view.clone_bytes_received_ = report->received.bytes;
    client = liquid::drive_client{};

    auto loaded = load(path);
    loaded.clone_objects_received_ = view.clone_objects_received_;
    loaded.clone_bytes_received_ = view.clone_bytes_received_;
    return loaded;
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
