// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/error.hpp"
#include "liquid/frontend_abi.h"
#include "liquid/session.hpp"

namespace liquid {
class query_registry;
}

namespace liquify {

class PluginHost;

struct PluginCommand {
  std::string plugin;
  std::string name;
  std::string default_binding;
  std::vector<std::byte> argument_schema_cbor;
};

struct PluginPanelSnapshot {
  std::string plugin;
  std::string name;
  std::vector<std::byte> view_model_cbor;
  std::string error;
  // No answer yet (liquify#149): the panel has been asked and draws its loading state.
  bool loading = false;
};

// What a command sent through `PluginDispatcher` came back with.
struct PluginCommandOutcome {
  std::string name;
  std::vector<std::byte> result_cbor;
  std::string error; // empty when the command succeeded
};

struct PluginTextPanel {
  std::string title;
  std::string text;
};

struct PluginLoadFailure {
  std::filesystem::path path;
  std::string error;
};

// A plugin a repository names that the user has not trusted. It is listed
// and never loaded.
struct UntrustedPlugin {
  std::filesystem::path path;
  std::filesystem::path repository;
  std::string digest;
};

struct ConfiguredPlugins {
  std::shared_ptr<PluginHost> host;
  std::vector<std::filesystem::path> paths;
  std::vector<PluginLoadFailure> failures;
  std::vector<UntrustedPlugin> untrusted;
};

enum class PluginSource { global, repository };

// What opening a repository would do with each configured plugin, without
// loading any of them.
struct PluginStatus {
  std::filesystem::path path;
  PluginSource source = PluginSource::global;
  std::string digest;
  bool trusted = false;
  std::string error;
};

class PluginHost {
public:
  PluginHost();
  ~PluginHost();
  PluginHost(PluginHost &&) noexcept;
  auto operator=(PluginHost &&) noexcept -> PluginHost &;
  PluginHost(const PluginHost &) = delete;
  auto operator=(const PluginHost &) -> PluginHost & = delete;

  // A plugin compiled into this binary; it runs in this process.
  [[nodiscard]] auto add(const liquify_plugin_v1 *descriptor)
      -> liquid::result<void>;
  // A plugin library, mapped only by its own sandboxed `liquify-plugin-host` process
  // (liquify#82). A crash or hang there fails the call that met it, never the IDE.
  [[nodiscard]] auto load(std::filesystem::path shared_object)
      -> liquid::result<void>;

  [[nodiscard]] auto query_names() const -> std::vector<std::string>;
  [[nodiscard]] auto commands() const -> std::vector<PluginCommand>;
  [[nodiscard]] auto invoke_command(std::string_view name,
                                    std::span<const std::byte> arguments_cbor)
      -> liquid::result<std::vector<std::byte>>;
  [[nodiscard]] auto panels(std::span<const std::byte> context_cbor)
      -> std::vector<PluginPanelSnapshot>;
  // The panels of one plugin, asked in registration order.
  [[nodiscard]] auto panels(std::string_view plugin,
                            std::span<const std::byte> context_cbor)
      -> std::vector<PluginPanelSnapshot>;
  // Every panel, unasked: each entry is `loading`.
  [[nodiscard]] auto panel_list() const -> std::vector<PluginPanelSnapshot>;
  // The loaded plugins, in load order.
  [[nodiscard]] auto plugin_names() const -> std::vector<std::string>;

  [[nodiscard]] auto register_queries(liquid::query_registry &registry) const
      -> liquid::result<void>;
  [[nodiscard]] auto invoke_query(std::string_view name,
                                  std::span<const std::byte> arguments_cbor)
      -> liquid::result<std::vector<std::byte>>;

  // An empty `plugin` publishes to every plugin; a name, to that plugin only.
  void publish_presence(const liquid::presence &value,
                        std::string_view plugin = {});
  void publish_lease(const liquid::semantic_lease &value, bool active = true,
                     std::string_view plugin = {});
  void publish_attribution(std::string_view path,
                           const liquid::change_id &change,
                           std::span<const liquid::line_attribution> lines,
                           std::string_view plugin = {});

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Sends panel refreshes, commands and events to the plugins from worker threads, so the UI
// thread never waits on a plugin (liquify#149). Each plugin has its own worker, which runs that
// plugin's requests one at a time in the order they were sent: a plugin that hangs delays only
// its own requests, each for at most its deadline (the host's, unchanged). The window reads the
// last answer of every panel, or its loading state, from `panels()`.
//
// No member waits for a plugin, the destructor included: a request still running when the
// dispatcher is destroyed finishes on its worker, which then exits, and its answer is dropped.
class PluginDispatcher {
public:
  // `on_answer` runs on a worker after each answer is recorded (to ask the window for a frame).
  // It must not call back into the dispatcher.
  explicit PluginDispatcher(std::shared_ptr<PluginHost> host,
                            std::function<void()> on_answer = {});
  ~PluginDispatcher();
  PluginDispatcher(const PluginDispatcher &) = delete;
  auto operator=(const PluginDispatcher &) -> PluginDispatcher & = delete;

  // Asks every plugin with panels for a refresh, unless one is already queued or running.
  void refresh_panels(std::span<const std::byte> context_cbor);
  // Queues the command on its plugin's worker; `not_found` when no plugin registered it.
  [[nodiscard]] auto invoke_command(std::string_view name,
                                    std::span<const std::byte> arguments_cbor)
      -> liquid::result<void>;
  void publish_presence(const liquid::presence &value);
  void publish_lease(const liquid::semantic_lease &value, bool active = true);
  void publish_attribution(std::string_view path,
                           const liquid::change_id &change,
                           std::span<const liquid::line_attribution> lines);

  // Every panel in registration order: its last answer (view model or error), or `loading`.
  [[nodiscard]] auto panels() const -> std::vector<PluginPanelSnapshot>;
  // The commands that have answered since the last call, in the order they answered.
  [[nodiscard]] auto take_command_outcomes() -> std::vector<PluginCommandOutcome>;
  // The commands sent and not yet answered.
  [[nodiscard]] auto running_commands() const -> std::vector<std::string>;
  // Requests queued or running, on every worker.
  [[nodiscard]] auto pending() const -> std::size_t;
  // Counts the answers recorded; changes whenever `panels()` or the outcomes do.
  [[nodiscard]] auto generation() const -> std::uint64_t;

  struct State;

private:
  std::shared_ptr<State> state_;
};

[[nodiscard]] auto canonical_plugin_cbor(std::span<const std::byte> value)
    -> liquid::result<std::vector<std::byte>>;
[[nodiscard]] auto decode_plugin_text_panel(std::span<const std::byte> value)
    -> liquid::result<PluginTextPanel>;
[[nodiscard]] auto plugin_null_cbor() -> std::span<const std::byte>;
[[nodiscard]] auto plugin_empty_map_cbor() -> std::span<const std::byte>;
[[nodiscard]] auto load_global_plugins(
    std::optional<std::filesystem::path> global_config_path = std::nullopt)
    -> liquid::result<ConfiguredPlugins>;
// Plugins named by repository metadata load only when their SHA-256 digest is
// in the user's trust store, which lives outside every repository. Plugins in
// the user's global configuration load as configured.
[[nodiscard]] auto load_configured_plugins(
    const std::filesystem::path &repository_path,
    std::optional<std::filesystem::path> global_config_path = std::nullopt,
    std::optional<std::filesystem::path> trust_store_path = std::nullopt)
    -> liquid::result<ConfiguredPlugins>;
[[nodiscard]] auto inspect_configured_plugins(
    const std::filesystem::path &repository_path,
    std::optional<std::filesystem::path> global_config_path = std::nullopt,
    std::optional<std::filesystem::path> trust_store_path = std::nullopt)
    -> liquid::result<std::vector<PluginStatus>>;

// The per-user trust store: one `sha256:<hex>` digest per line, optionally
// followed by the path it was trusted from. Beside the user settings file.
[[nodiscard]] auto plugin_trust_store_path() -> std::filesystem::path;
// The user's global plugin list (liquify#72): its own file, `plugins` beside the user settings
// file, in `config.liquify.plugins=PATH[,PATH...]` lines. It is not the settings file, whose
// `liquify settings { ... }` syntax the plugin reader cannot parse, and vice versa.
[[nodiscard]] auto global_plugin_config_path() -> std::filesystem::path;
[[nodiscard]] auto plugin_digest(const std::filesystem::path &shared_object)
    -> liquid::result<std::string>;
[[nodiscard]] auto trusted_plugin_digests(
    const std::filesystem::path &trust_store_path)
    -> liquid::result<std::vector<std::string>>;
// Records the digest of the file at `shared_object` in the trust store and
// returns it. This is the only way a repository-named plugin becomes loadable.
[[nodiscard]] auto trust_plugin(const std::filesystem::path &shared_object,
                                const std::filesystem::path &trust_store_path)
    -> liquid::result<std::string>;
// Records a digest the user has already seen and confirmed, with the path it
// came from as a comment.
[[nodiscard]] auto trust_plugin_digest(
    std::string_view digest, const std::filesystem::path &origin,
    const std::filesystem::path &trust_store_path) -> liquid::result<void>;

} // namespace liquify
