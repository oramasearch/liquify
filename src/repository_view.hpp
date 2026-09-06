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
    [[nodiscard]] static DirectoryLoad load_directory(std::filesystem::path repository_path,
                                                      liquid::digest id);

    std::filesystem::path repository_path_;
    std::vector<liquid::tree_entry> entries_;
    std::map<std::string, DirectoryState, std::less<>> directories_;
    std::string error_;
    std::chrono::duration<double, std::milli> open_time_{};
};

[[nodiscard]] auto node_kind_name(liquid::node_kind kind) -> std::string_view;

} // namespace liquify
