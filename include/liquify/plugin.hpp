#pragma once

#include <cstddef>
#include <filesystem>
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

  [[nodiscard]] auto add(const liquify_plugin_v1 *descriptor)
      -> liquid::result<void>;
  [[nodiscard]] auto load(std::filesystem::path shared_object)
      -> liquid::result<void>;

  [[nodiscard]] auto query_names() const -> std::vector<std::string>;
  [[nodiscard]] auto commands() const -> std::vector<PluginCommand>;
  [[nodiscard]] auto invoke_command(std::string_view name,
                                    std::span<const std::byte> arguments_cbor)
      -> liquid::result<std::vector<std::byte>>;
  [[nodiscard]] auto panels(std::span<const std::byte> context_cbor)
      -> std::vector<PluginPanelSnapshot>;

  [[nodiscard]] auto register_queries(liquid::query_registry &registry) const
      -> liquid::result<void>;
  [[nodiscard]] auto invoke_query(std::string_view name,
                                  std::span<const std::byte> arguments_cbor)
      -> liquid::result<std::vector<std::byte>>;

  void publish_presence(const liquid::presence &value);
  void publish_lease(const liquid::semantic_lease &value, bool active = true);
  void publish_attribution(std::string_view path,
                           const liquid::change_id &change,
                           std::span<const liquid::line_attribution> lines);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
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
