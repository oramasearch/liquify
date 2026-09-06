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

struct ConfiguredPlugins {
    std::shared_ptr<PluginHost> host;
    std::vector<std::filesystem::path> paths;
    std::vector<PluginLoadFailure> failures;
};

class PluginHost {
public:
    PluginHost();
    ~PluginHost();
    PluginHost(PluginHost&&) noexcept;
    auto operator=(PluginHost&&) noexcept -> PluginHost&;
    PluginHost(const PluginHost&) = delete;
    auto operator=(const PluginHost&) -> PluginHost& = delete;

    [[nodiscard]] auto add(const liquify_plugin_v1* descriptor) -> liquid::result<void>;
    [[nodiscard]] auto load(std::filesystem::path shared_object) -> liquid::result<void>;

    [[nodiscard]] auto query_names() const -> std::vector<std::string>;
    [[nodiscard]] auto commands() const -> std::vector<PluginCommand>;
    [[nodiscard]] auto invoke_command(std::string_view name,
                                      std::span<const std::byte> arguments_cbor)
        -> liquid::result<std::vector<std::byte>>;
    [[nodiscard]] auto panels(std::span<const std::byte> context_cbor)
        -> std::vector<PluginPanelSnapshot>;

    [[nodiscard]] auto register_queries(liquid::query_registry& registry) const
        -> liquid::result<void>;
    [[nodiscard]] auto invoke_query(std::string_view name,
                                    std::span<const std::byte> arguments_cbor)
        -> liquid::result<std::vector<std::byte>>;

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
[[nodiscard]] auto
load_configured_plugins(const std::filesystem::path& repository_path,
                        std::optional<std::filesystem::path> global_config_path = std::nullopt)
    -> liquid::result<ConfiguredPlugins>;

} // namespace liquify
