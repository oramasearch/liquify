#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/object.hpp"

namespace liquify {

class RepositoryView {
public:
    [[nodiscard]] static RepositoryView open(std::filesystem::path path);
    [[nodiscard]] static RepositoryView
    clone(std::filesystem::path path, std::string remote, std::string bearer_token = {});

    [[nodiscard]] auto path() const -> const std::filesystem::path&;
    [[nodiscard]] auto name() const -> std::string;
    [[nodiscard]] auto entries() const -> std::span<const liquid::tree_entry>;
    [[nodiscard]] auto entries(std::string_view directory) const
        -> std::span<const liquid::tree_entry>;
    [[nodiscard]] auto error() const -> std::string_view;
    [[nodiscard]] auto directory_error(std::string_view directory) const -> std::string_view;
    [[nodiscard]] auto directory_loading(std::string_view directory) const -> bool;
    [[nodiscard]] auto directory_loaded(std::string_view directory) const -> bool;
    [[nodiscard]] auto loading_directory_count() const -> std::size_t;
    [[nodiscard]] auto loaded_directory_count() const -> std::size_t;
    [[nodiscard]] auto directory_error_count() const -> std::size_t;
    [[nodiscard]] auto open_time() const -> std::chrono::duration<double, std::milli>;
    [[nodiscard]] auto branch() const -> std::string_view;
    [[nodiscard]] auto current_change() const -> std::string_view;
    [[nodiscard]] auto last_change() const -> std::string_view;
    [[nodiscard]] auto last_changed_at() const -> std::string_view;
    [[nodiscard]] auto offline_reason() const -> std::string_view;
    [[nodiscard]] auto offline() const -> bool;
    [[nodiscard]] auto remote_bytes() const -> std::uint64_t;
    [[nodiscard]] auto hydration_debt() const -> std::uint64_t;
    [[nodiscard]] auto pending_uploads() const -> std::uint64_t;
    [[nodiscard]] auto presence_count() const -> std::size_t;
    [[nodiscard]] auto clone_objects_received() const -> std::uint64_t;
    [[nodiscard]] auto clone_bytes_received() const -> std::uint64_t;

    void request_directory(std::string path, liquid::digest id);
    void poll();

private:
    struct DirectoryLoad {
        std::vector<liquid::tree_entry> entries;
        std::string error;
    };

    struct DirectoryState {
        std::vector<liquid::tree_entry> entries;
        std::string error;
        std::optional<std::future<DirectoryLoad>> pending;
        bool loaded{};
    };

    [[nodiscard]] static RepositoryView load(std::filesystem::path path);
    [[nodiscard]] static RepositoryView
    clone_impl(std::filesystem::path path, std::string remote, std::string bearer_token);
    [[nodiscard]] static DirectoryLoad load_directory(std::filesystem::path repository_path,
                                                      liquid::digest id);

    std::filesystem::path repository_path_;
    std::vector<liquid::tree_entry> entries_;
    std::map<std::string, DirectoryState, std::less<>> directories_;
    std::string error_;
    std::string branch_;
    std::string current_change_;
    std::string last_change_;
    std::string last_changed_at_;
    std::string offline_reason_;
    bool offline_{};
    std::uint64_t remote_bytes_{};
    std::uint64_t hydration_debt_{};
    std::uint64_t pending_uploads_{};
    std::size_t presence_count_{};
    std::uint64_t clone_objects_received_{};
    std::uint64_t clone_bytes_received_{};
    std::chrono::duration<double, std::milli> open_time_{};
};

[[nodiscard]] auto node_kind_name(liquid::node_kind kind) -> std::string_view;

} // namespace liquify
