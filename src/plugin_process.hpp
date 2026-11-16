// SPDX-License-Identifier: MIT

#pragma once

// One `liquify-plugin-host` process per dynamic plugin (liquify#82). The IDE never maps a plugin
// library: it starts the host, reads the manifest the host reports, and sends each query,
// command, panel and event as one framed request. A call that misses its deadline, a host that
// exits, and a malformed frame all kill the host and become that call's error; the next call
// starts it again, up to `max_restarts` times.

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/error.hpp"
#include "plugin_protocol.hpp"

namespace liquify::detail {

class PluginProcess {
public:
  static constexpr int max_restarts = 3;
  static constexpr auto start_deadline = std::chrono::seconds(10);

  [[nodiscard]] static auto start(std::filesystem::path host, std::filesystem::path plugin)
      -> liquid::result<std::shared_ptr<PluginProcess>>;

  PluginProcess(const PluginProcess &) = delete;
  auto operator=(const PluginProcess &) -> PluginProcess & = delete;
  ~PluginProcess();

  [[nodiscard]] auto manifest() const -> const plugin_protocol::Manifest & { return manifest_; }

  // Thread-safe: graph workers and the plugin's dispatcher worker share one host, one call at a
  // time.
  [[nodiscard]] auto call(plugin_protocol::Kind kind, std::string_view name,
                          std::span<const std::byte> input, std::chrono::milliseconds deadline)
      -> liquid::result<std::vector<std::byte>>;

  struct Child;

private:
  PluginProcess(std::filesystem::path host, std::filesystem::path plugin);

  [[nodiscard]] auto exchange(const plugin_protocol::Request &request,
                              std::chrono::milliseconds deadline)
      -> liquid::result<plugin_protocol::Response>;
  void stop();

  std::filesystem::path host_;
  std::filesystem::path plugin_;
  plugin_protocol::Manifest manifest_;
  std::unique_ptr<Child> child_;
  int failures_{};
  liquid::error last_error_{};
  std::mutex mutex_;
};

// `liquify-plugin-host` beside the running executable, or the build tree's copy.
[[nodiscard]] auto plugin_host_executable() -> std::filesystem::path;

} // namespace liquify::detail
