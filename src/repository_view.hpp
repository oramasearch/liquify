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
#include "liquify/navigation.hpp"
#include "liquify/user_error.hpp"

namespace liquify {

class RepositoryView {
public:
  [[nodiscard]] static RepositoryView open(std::filesystem::path path);
  [[nodiscard]] static RepositoryView clone(std::filesystem::path path,
                                            std::string remote,
                                            std::string bearer_token = {});

  [[nodiscard]] auto path() const -> const std::filesystem::path &;
  [[nodiscard]] auto name() const -> std::string;
  [[nodiscard]] auto entries() const -> std::span<const liquid::tree_entry>;
  [[nodiscard]] auto entries(std::string_view directory) const
      -> std::span<const liquid::tree_entry>;
  [[nodiscard]] auto error() const -> std::string_view;
  [[nodiscard]] auto directory_error(std::string_view directory) const
      -> std::string_view;
  [[nodiscard]] auto directory_loading(std::string_view directory) const
      -> bool;
  [[nodiscard]] auto directory_loaded(std::string_view directory) const -> bool;
  [[nodiscard]] auto loading_directory_count() const -> std::size_t;
  [[nodiscard]] auto loaded_directory_count() const -> std::size_t;
  [[nodiscard]] auto directory_error_count() const -> std::size_t;
  [[nodiscard]] auto open_time() const
      -> std::chrono::duration<double, std::milli>;
  [[nodiscard]] auto navigation_paths() const
      -> std::span<const NavigationPath>;
  [[nodiscard]] auto navigation_symbols() const
      -> std::span<const NavigationSymbol>;
  [[nodiscard]] auto navigation_loading() const -> bool;
  [[nodiscard]] auto navigation_error() const -> std::string_view;
  [[nodiscard]] auto metadata(std::string_view path) const
      -> const NavigationPath *;
  [[nodiscard]] auto mutation_running() const -> bool;
  [[nodiscard]] auto mutation_status() const -> std::string_view;
  [[nodiscard]] auto branch() const -> std::string_view;
  [[nodiscard]] auto current_change() const -> std::string_view;
  [[nodiscard]] auto last_change() const -> std::string_view;
  [[nodiscard]] auto last_changed_at() const -> std::string_view;
  [[nodiscard]] auto offline_reason() const -> std::string_view;
  [[nodiscard]] auto offline() const -> bool;
  // Why a clone failed, in the product's words; empty after a successful clone or an open.
  [[nodiscard]] auto clone_failure() const -> const UserError &;
  [[nodiscard]] auto remote_bytes() const -> std::uint64_t;
  [[nodiscard]] auto hydration_debt() const -> std::uint64_t;
  [[nodiscard]] auto pending_uploads() const -> std::uint64_t;
  [[nodiscard]] auto presence_count() const -> std::size_t;
  [[nodiscard]] auto clone_objects_received() const -> std::uint64_t;
  [[nodiscard]] auto clone_bytes_received() const -> std::uint64_t;

  void request_directory(std::string path, liquid::digest id);
  void request_move(std::string from, std::string to);
  void request_new_file(std::string path);
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

  struct NavigationLoad {
    std::vector<NavigationPath> paths;
    std::vector<NavigationSymbol> symbols;
    std::string error;
  };

  struct MutationLoad {
    std::vector<liquid::tree_entry> entries;
    std::vector<std::string> modified;
    std::vector<std::string> in_change;
    std::vector<std::string> conflicted;
    std::string error;
    std::string status;
  };

  [[nodiscard]] static RepositoryView load(std::filesystem::path path);
  [[nodiscard]] static RepositoryView clone_impl(std::filesystem::path path,
                                                 std::string remote,
                                                 std::string bearer_token);
  [[nodiscard]] static DirectoryLoad
  load_directory(std::filesystem::path repository_path, liquid::digest id);
  [[nodiscard]] static NavigationLoad load_navigation(
      std::filesystem::path repository_path, std::vector<std::string> modified,
      std::vector<std::string> in_change, std::vector<std::string> conflicted);
  [[nodiscard]] static MutationLoad
  move_path(std::filesystem::path repository_path, std::string from,
            std::string to);
  [[nodiscard]] static MutationLoad
  create_file(std::filesystem::path repository_path, std::string path);
  void start_navigation_load();

  std::filesystem::path repository_path_;
  std::vector<liquid::tree_entry> entries_;
  std::map<std::string, DirectoryState, std::less<>> directories_;
  std::vector<NavigationPath> navigation_paths_;
  std::vector<NavigationSymbol> navigation_symbols_;
  std::vector<std::string> modified_paths_;
  std::vector<std::string> in_change_paths_;
  std::vector<std::string> conflicted_paths_;
  std::optional<std::future<NavigationLoad>> navigation_pending_;
  std::optional<std::future<MutationLoad>> mutation_pending_;
  std::string error_;
  std::string navigation_error_;
  std::string mutation_status_;
  std::string branch_;
  std::string current_change_;
  std::string last_change_;
  std::string last_changed_at_;
  std::string offline_reason_;
  bool offline_{};
  UserError clone_failure_;
  std::uint64_t remote_bytes_{};
  std::uint64_t hydration_debt_{};
  std::uint64_t pending_uploads_{};
  std::size_t presence_count_{};
  std::uint64_t clone_objects_received_{};
  std::uint64_t clone_bytes_received_{};
  std::chrono::duration<double, std::milli> open_time_{};
  bool navigation_reload_pending_{};
};

[[nodiscard]] auto node_kind_name(liquid::node_kind kind) -> std::string_view;

} // namespace liquify
