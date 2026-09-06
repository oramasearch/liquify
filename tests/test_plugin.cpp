#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/interactive_analysis.hpp"
#include "liquify/plugin.hpp"
#include "sample_plugin.hpp"

namespace {

class TemporaryDirectory {
public:
  explicit TemporaryDirectory(std::string_view prefix) {
    static std::atomic_uint64_t suffix{};
    path_ = std::filesystem::temp_directory_path() /
            (std::string(prefix) + "-" + std::to_string(++suffix));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] auto path() const -> const std::filesystem::path & {
    return path_;
  }

private:
  std::filesystem::path path_;
};

auto bytes(std::span<const std::uint8_t> value) -> std::vector<std::byte> {
  auto output = std::vector<std::byte>(value.size());
  std::memcpy(output.data(), value.data(), value.size());
  return output;
}

auto text_bytes(std::string_view value) -> std::vector<std::byte> {
  const auto characters = std::span(value.data(), value.size());
  const auto value_bytes = std::as_bytes(characters);
  return {value_bytes.begin(), value_bytes.end()};
}

void release_bytes(void *, std::uint8_t *data, std::size_t) { std::free(data); }

auto copy_result(std::span<const std::uint8_t> value,
                 liquid_frontend_owned_bytes_v1 *output) -> int {
  auto *data = static_cast<std::uint8_t *>(std::malloc(value.size()));
  if (!data) {
    return LIQUID_FRONTEND_STATUS_INTERNAL;
  }
  std::memcpy(data, value.data(), value.size());
  *output = {data, value.size(), release_bytes, nullptr};
  return LIQUID_FRONTEND_STATUS_OK;
}

auto broken_panel(void *, liquid_frontend_bytes_v1,
                  const liquid_frontend_callbacks_v1 *,
                  liquid_frontend_owned_bytes_v1 *,
                  liquid_frontend_error_v1 *error) -> int {
  error->code = LIQUID_FRONTEND_STATUS_INTERNAL;
  error->message = "sample panel fault";
  return LIQUID_FRONTEND_STATUS_INTERNAL;
}

auto healthy_panel(void *, liquid_frontend_bytes_v1,
                   const liquid_frontend_callbacks_v1 *,
                   liquid_frontend_owned_bytes_v1 *output,
                   liquid_frontend_error_v1 *) -> int {
  static constexpr std::uint8_t model[] = {
      0xa3, 0x64, 'k', 'i',  'n', 'd',  0x64, 't', 'e',  'x', 't',
      0x64, 't',  'e', 'x',  't', 0x62, 'o',  'k', 0x65, 't', 'i',
      't',  'l',  'e', 0x67, 'H', 'e',  'a',  'l', 't',  'h', 'y'};
  return copy_result(model, output);
}

const liquify_plugin_panel_v1 fault_panels[] = {
    {"fault.broken", broken_panel}, {"fault.healthy", healthy_panel}};
const liquify_plugin_v1 fault_descriptor = {LIQUIFY_PLUGIN_ABI_VERSION,
                                            sizeof(liquify_plugin_v1),
                                            "fault",
                                            nullptr,
                                            0,
                                            nullptr,
                                            0,
                                            fault_panels,
                                            2,
                                            nullptr,
                                            nullptr};

void exercise_sample(const std::shared_ptr<liquify::PluginHost> &host,
                     const std::filesystem::path &state) {
  const auto query_names = host->query_names();
  ASSERT_EQ(query_names, (std::vector<std::string>{"sample.echo"}));
  const auto commands = host->commands();
  ASSERT_EQ(commands.size(), 1);
  EXPECT_EQ(commands.front().plugin, "sample");
  EXPECT_EQ(commands.front().name, "sample.reset");
  EXPECT_EQ(commands.front().default_binding, "Primary+Shift+R");

  auto analysis = liquify::InteractiveAnalysis::open(state, host);
  ASSERT_TRUE(analysis) << analysis.error().message;
  auto query =
      analysis->plugin_query("sample.echo", liquify::plugin_null_cbor());
  ASSERT_TRUE(query) << query.error().message;
  EXPECT_EQ(*query, std::vector<std::byte>(liquify::plugin_null_cbor().begin(),
                                           liquify::plugin_null_cbor().end()));

  auto command =
      host->invoke_command("sample.reset", liquify::plugin_empty_map_cbor());
  ASSERT_TRUE(command) << command.error().message;
  static constexpr std::uint8_t expected_command[] = {0xa1, 0x62, 'o', 'k',
                                                      0xf5};
  EXPECT_EQ(*command, bytes(expected_command));

  auto panels = host->panels(liquify::plugin_null_cbor());
  ASSERT_EQ(panels.size(), 1);
  ASSERT_TRUE(panels.front().error.empty()) << panels.front().error;
  auto panel =
      liquify::decode_plugin_text_panel(panels.front().view_model_cbor);
  ASSERT_TRUE(panel) << panel.error().message;
  EXPECT_EQ(panel->title, "Sample plugin");
  EXPECT_EQ(panel->text, "Sample plugin ready.");
}

} // namespace

TEST(PluginHost, RegistersTheSampleStatically) {
  auto location = TemporaryDirectory("liquify-plugin-static");
  auto host = std::make_shared<liquify::PluginHost>();
  auto added = host->add(liquify_sample_plugin_v1());
  ASSERT_TRUE(added) << added.error().message;
  exercise_sample(host, location.path());
}

TEST(PluginHost, LoadsTheSampleDynamically) {
  auto location = TemporaryDirectory("liquify-plugin-dynamic");
  auto host = std::make_shared<liquify::PluginHost>();
  auto loaded = host->load(LIQUIFY_SAMPLE_PLUGIN);
  ASSERT_TRUE(loaded) << loaded.error().message << ": "
                      << loaded.error().context;
  exercise_sample(host, location.path());
}

TEST(PluginHost, LoadsGlobalPluginsWithoutARepository) {
  auto location = TemporaryDirectory("liquify-plugin-global");
  const auto config_path = location.path() / "config";
  auto config = std::ofstream(config_path);
  ASSERT_TRUE(config);
  config << "config.liquify.plugins=" << LIQUIFY_SAMPLE_PLUGIN << "\n";
  config.close();

  auto plugins = liquify::load_global_plugins(config_path);
  ASSERT_TRUE(plugins) << plugins.error().message;
  EXPECT_TRUE(plugins->failures.empty());
  exercise_sample(plugins->host, location.path() / "state");
}

TEST(PluginHost, LoadsConfiguredPluginsAndReportsFailures) {
  auto location = TemporaryDirectory("liquify-plugin-config");
  const auto repository_path = location.path() / "repository";
  auto repository =
      liquid::runtime::block_on(liquid::repository::create(repository_path));
  ASSERT_TRUE(repository) << repository.error().message;
  auto configured_repository =
      liquid::runtime::block_on(repository->set_metadata(
          "config.liquify.plugins", text_bytes(LIQUIFY_SAMPLE_PLUGIN)));
  ASSERT_TRUE(configured_repository) << configured_repository.error().message;

  const auto config_path = location.path() / "config";
  auto config = std::ofstream(config_path);
  ASSERT_TRUE(config);
  config << "config.liquify.plugins=missing-plugin\n";
  config << "config.liquify.plugins=" << LIQUIFY_SAMPLE_PLUGIN << "\n";
  config.close();

  auto plugins = liquify::load_configured_plugins(repository_path, config_path);
  ASSERT_TRUE(plugins) << plugins.error().message;
  ASSERT_EQ(plugins->paths.size(), 2);
  ASSERT_EQ(plugins->failures.size(), 1);
  EXPECT_EQ(plugins->failures.front().path, location.path() / "missing-plugin");
  exercise_sample(plugins->host, location.path() / "state");
}

TEST(PluginHost, RefusesAnAbiVersionMismatch) {
  auto descriptor = *liquify_sample_plugin_v1();
  descriptor.abi_version = LIQUIFY_PLUGIN_ABI_VERSION + 1;
  auto host = liquify::PluginHost{};
  auto added = host.add(&descriptor);
  ASSERT_FALSE(added);
  EXPECT_EQ(added.error().code, liquid::errc::invalid_argument);
  EXPECT_EQ(added.error().message,
            "plugin descriptor does not implement ABI v1");
}

TEST(PluginHost, IsolatesAPanelFault) {
  auto host = liquify::PluginHost{};
  auto added = host.add(&fault_descriptor);
  ASSERT_TRUE(added) << added.error().message;
  const auto panels = host.panels(liquify::plugin_null_cbor());
  ASSERT_EQ(panels.size(), 2);
  EXPECT_NE(panels[0].error.find("sample panel fault"), std::string::npos);
  EXPECT_TRUE(panels[1].error.empty());
  auto healthy = liquify::decode_plugin_text_panel(panels[1].view_model_cbor);
  ASSERT_TRUE(healthy) << healthy.error().message;
  EXPECT_EQ(healthy->title, "Healthy");
  EXPECT_EQ(healthy->text, "ok");
}

TEST(PluginHost, RejectsNonCanonicalCbor) {
  static constexpr std::uint8_t non_minimal_zero[] = {0x18, 0x00};
  auto canonical = liquify::canonical_plugin_cbor(bytes(non_minimal_zero));
  ASSERT_FALSE(canonical);
  EXPECT_EQ(canonical.error().code, liquid::errc::invalid_argument);
}
