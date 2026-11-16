// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/interactive_analysis.hpp"
#include "liquify/plugin.hpp"
#include "liquify/settings.hpp"
#include "plugin_protocol.hpp"
#include "sample_plugin.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

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

struct EventState {
  std::size_t presence_count{};
  std::size_t lease_count{};
  std::size_t attribution_count{};
  std::vector<std::byte> last;
};

void copy_event(std::size_t &count, EventState &state,
                liquid_frontend_bytes_v1 value) {
  ++count;
  const auto bytes =
      std::span(reinterpret_cast<const std::byte *>(value.data), value.size);
  state.last.assign(bytes.begin(), bytes.end());
}

void event_presence(void *context, liquid_frontend_bytes_v1 value) {
  auto &state = *static_cast<EventState *>(context);
  copy_event(state.presence_count, state, value);
}

void event_lease(void *context, liquid_frontend_bytes_v1 value) {
  auto &state = *static_cast<EventState *>(context);
  copy_event(state.lease_count, state, value);
}

void event_attribution(void *context, liquid_frontend_bytes_v1 value) {
  auto &state = *static_cast<EventState *>(context);
  copy_event(state.attribution_count, state, value);
}

const liquify_plugin_event_callbacks_v1 event_callbacks = {
    LIQUIFY_PLUGIN_EVENTS_ABI_VERSION,
    sizeof(liquify_plugin_event_callbacks_v1), event_presence, event_lease,
    event_attribution};

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

// liquify#72: the global plugin list and the settings file are two files in two formats, so
// saving settings never breaks plugin loading and a plugin line never breaks the settings parse.
TEST(PluginConfiguration, GlobalPluginsHaveTheirOwnFileApartFromSettings) {
  const auto plugins = liquify::global_plugin_config_path();
  const auto settings = liquify::user_settings_path();
  if (settings.empty()) {
    GTEST_SKIP() << "no user settings directory in this environment";
  }
  EXPECT_NE(plugins, settings);
  EXPECT_EQ(plugins.parent_path(), settings.parent_path());
  EXPECT_EQ(plugins.filename(), "plugins");
  EXPECT_NE(plugins, liquify::plugin_trust_store_path());
}

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

  auto plugins = liquify::load_configured_plugins(
      repository_path, config_path, location.path() / "trusted-plugins");
  ASSERT_TRUE(plugins) << plugins.error().message;
  ASSERT_EQ(plugins->paths.size(), 2);
  EXPECT_TRUE(plugins->untrusted.empty());
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

TEST(PluginHost, DeliversMultiplayerEventsThroughTheAbi) {
  auto state = EventState{};
  const auto descriptor = liquify_plugin_v1{LIQUIFY_PLUGIN_ABI_VERSION,
                                            sizeof(liquify_plugin_v1),
                                            "events",
                                            nullptr,
                                            0,
                                            nullptr,
                                            0,
                                            nullptr,
                                            0,
                                            &state,
                                            nullptr,
                                            &event_callbacks};
  auto host = liquify::PluginHost{};
  ASSERT_TRUE(host.add(&descriptor));

  auto presence = liquid::presence{};
  presence.actor.taap_id = "person";
  presence.path = "main.cpp";
  presence.activity = liquid::presence_activity::editing;
  host.publish_presence(presence);

  auto lease = liquid::semantic_lease{};
  lease.by.taap_id = "person";
  lease.claim.path = "main.cpp";
  lease.claim.kind = liquid::lease_kind::file;
  host.publish_lease(lease);

  auto attribution = liquid::line_attribution{};
  attribution.line = 1;
  attribution.by.taap_id = "agent";
  attribution.agent = true;
  const auto change = liquid::change_id{};
  host.publish_attribution("main.cpp", change,
                           std::span{&attribution, std::size_t{1}});

  EXPECT_EQ(state.presence_count, 1);
  EXPECT_EQ(state.lease_count, 1);
  EXPECT_EQ(state.attribution_count, 1);
  EXPECT_FALSE(state.last.empty());
  EXPECT_TRUE(liquify::canonical_plugin_cbor(state.last));
}

TEST(PluginHost, RejectsNonCanonicalCbor) {
  static constexpr std::uint8_t non_minimal_zero[] = {0x18, 0x00};
  auto canonical = liquify::canonical_plugin_cbor(bytes(non_minimal_zero));
  ASSERT_FALSE(canonical);
  EXPECT_EQ(canonical.error().code, liquid::errc::invalid_argument);
}

// liquify#82: a dynamic plugin runs in its own `liquify-plugin-host` process.
namespace {

auto text_cbor(std::string_view value) -> std::vector<std::byte> {
  auto output = std::vector<std::byte>{};
  if (value.size() < 24) {
    output.push_back(static_cast<std::byte>(0x60U + value.size()));
  } else if (value.size() <= 0xffU) {
    output.push_back(std::byte{0x78});
    output.push_back(static_cast<std::byte>(value.size()));
  } else {
    output.push_back(std::byte{0x79});
    output.push_back(static_cast<std::byte>(value.size() >> 8U));
    output.push_back(static_cast<std::byte>(value.size() & 0xffU));
  }
  const auto text = text_bytes(value);
  output.insert(output.end(), text.begin(), text.end());
  return output;
}

auto cbor_boolean(bool value) -> std::vector<std::byte> {
  return {value ? std::byte{0xf5} : std::byte{0xf4}};
}

auto library_is_mapped(const char *path) -> bool {
#if defined(_WIN32)
  return GetModuleHandleW(std::filesystem::path(path).wstring().c_str()) != nullptr;
#else
  auto *library = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
  if (library != nullptr) {
    dlclose(library);
    return true;
  }
  return false;
#endif
}

auto load_fault_plugin() -> std::shared_ptr<liquify::PluginHost> {
  auto host = std::make_shared<liquify::PluginHost>();
  auto loaded = host->load(LIQUIFY_FAULT_PLUGIN);
  EXPECT_TRUE(loaded) << loaded.error().message << ": " << loaded.error().context;
  return host;
}

} // namespace

TEST(PluginProcess, TheIdeNeverMapsADynamicPlugin) {
  auto host = std::make_shared<liquify::PluginHost>();
  auto loaded = host->load(LIQUIFY_SAMPLE_PLUGIN);
  ASSERT_TRUE(loaded) << loaded.error().message << ": " << loaded.error().context;
  EXPECT_FALSE(library_is_mapped(LIQUIFY_SAMPLE_PLUGIN));
  EXPECT_FALSE(host->commands().empty());
}

TEST(PluginProcess, ACrashingCommandFailsTheCallAndTheNextCallRestartsTheHost) {
  auto host = load_fault_plugin();
  auto crashed = host->invoke_command("fault.crash", liquify::plugin_empty_map_cbor());
  ASSERT_FALSE(crashed);
  EXPECT_EQ(crashed.error().code, liquid::errc::unavailable);
  EXPECT_EQ(crashed.error().context, "fault:fault.crash");
#if !defined(_WIN32)
  EXPECT_NE(crashed.error().message.find("signal"), std::string::npos)
      << crashed.error().message;
#endif
  auto environment = host->invoke_command("fault.environment", liquify::plugin_empty_map_cbor());
  ASSERT_TRUE(environment) << environment.error().message;
}

TEST(PluginProcess, AHangingPanelBecomesThatPanelsErrorAndAnotherPanelStillRenders) {
  auto host = load_fault_plugin();
  const auto panels = host->panels(liquify::plugin_null_cbor());
  ASSERT_EQ(panels.size(), 2);
  EXPECT_EQ(panels[0].name, "fault.hang");
  EXPECT_NE(panels[0].error.find("did not answer in time"), std::string::npos)
      << panels[0].error;
  ASSERT_TRUE(panels[1].error.empty()) << panels[1].error;
  auto healthy = liquify::decode_plugin_text_panel(panels[1].view_model_cbor);
  ASSERT_TRUE(healthy) << healthy.error().message;
  EXPECT_EQ(healthy->title, "Healthy");
}

TEST(PluginProcess, AFailingPluginIsDisabledAfterItsRestartBudget) {
  auto host = load_fault_plugin();
  for (int attempt = 0; attempt < 4; ++attempt) {
    ASSERT_FALSE(host->invoke_command("fault.crash", liquify::plugin_empty_map_cbor()));
  }
  auto disabled = host->invoke_command("fault.environment", liquify::plugin_empty_map_cbor());
  ASSERT_FALSE(disabled);
  EXPECT_NE(disabled.error().message.find("disabled"), std::string::npos)
      << disabled.error().message;
}

TEST(PluginProcess, ThePluginCannotWriteFiles) {
  auto location = TemporaryDirectory("liquify-plugin-sandbox");
  const auto target = location.path() / "written-by-plugin";
  auto host = load_fault_plugin();
  auto written = host->invoke_command("fault.write", text_cbor(target.string()));
  ASSERT_TRUE(written) << written.error().message;
  EXPECT_EQ(*written, cbor_boolean(false));
  EXPECT_FALSE(std::filesystem::exists(target));
}

TEST(PluginProcess, ThePluginCannotOpenANetworkConnection) {
#if defined(_WIN32)
  GTEST_SKIP() << "the Windows host has no network sandbox (docs/plugins/README.md)";
#else
  auto host = load_fault_plugin();
  auto network = host->invoke_command("fault.network", liquify::plugin_empty_map_cbor());
  ASSERT_TRUE(network) << network.error().message;
  EXPECT_EQ(*network, cbor_boolean(false));
#endif
}

TEST(PluginProcess, ThePluginDoesNotSeeTheIdesEnvironment) {
#if defined(_WIN32)
  _putenv_s("LIQUIFY_PLUGIN_TEST_SECRET", "secret");
#else
  setenv("LIQUIFY_PLUGIN_TEST_SECRET", "secret", 1);
#endif
  auto host = load_fault_plugin();
  auto environment = host->invoke_command("fault.environment", liquify::plugin_empty_map_cbor());
#if defined(_WIN32)
  _putenv_s("LIQUIFY_PLUGIN_TEST_SECRET", "");
#else
  unsetenv("LIQUIFY_PLUGIN_TEST_SECRET");
#endif
  ASSERT_TRUE(environment) << environment.error().message;
  EXPECT_EQ(*environment, cbor_boolean(false));
}

TEST(PluginProcess, AMissingHostLibraryIsALoadFailure) {
  auto host = liquify::PluginHost{};
  auto loaded = host.load("missing-plugin-library");
  ASSERT_FALSE(loaded);
  EXPECT_NE(loaded.error().context.find("missing-plugin-library"), std::string::npos);
}

TEST(PluginProtocol, MessagesRoundTripAndMalformedFramesAreRefused) {
  namespace protocol = liquify::plugin_protocol;
  auto manifest = protocol::Manifest{};
  manifest.name = "sample";
  manifest.queries = {"sample.echo"};
  manifest.commands = {{"sample.reset", "Primary+Shift+R", {std::byte{0xa0}}}};
  manifest.panels = {"sample.status"};
  manifest.events = {"presence"};
  auto decoded = protocol::decode_manifest(protocol::encode(manifest));
  ASSERT_TRUE(decoded) << decoded.error();
  EXPECT_EQ(*decoded, manifest);

  auto failed = protocol::Manifest{};
  failed.error = "could not load plugin";
  auto decoded_failure = protocol::decode_manifest(protocol::encode(failed));
  ASSERT_TRUE(decoded_failure);
  EXPECT_EQ(decoded_failure->error, "could not load plugin");

  const auto request = protocol::Request{protocol::Kind::panel, "sample.status", {std::byte{0xf6}}};
  auto decoded_request = protocol::decode_request(protocol::encode(request));
  ASSERT_TRUE(decoded_request) << decoded_request.error();
  EXPECT_EQ(decoded_request->kind, protocol::Kind::panel);
  EXPECT_EQ(decoded_request->name, "sample.status");
  EXPECT_EQ(decoded_request->input, request.input);

  const auto response = protocol::Response{4, {}, "plugin operation failed"};
  auto decoded_response = protocol::decode_response(protocol::encode(response));
  ASSERT_TRUE(decoded_response) << decoded_response.error();
  EXPECT_EQ(decoded_response->status, 4U);
  EXPECT_EQ(decoded_response->message, "plugin operation failed");

  auto truncated = protocol::encode(request);
  truncated.pop_back();
  EXPECT_FALSE(protocol::decode_request(truncated));
  auto trailing = protocol::encode(response);
  trailing.push_back(std::byte{0});
  EXPECT_FALSE(protocol::decode_response(trailing));
  const auto unknown_kind = std::vector<std::byte>{std::byte{0x83}, std::byte{0x09},
                                                   std::byte{0x60}, std::byte{0x40}};
  EXPECT_FALSE(protocol::decode_request(unknown_kind));
  EXPECT_EQ(protocol::frame_size(protocol::frame_header(70'000)), 70'000U);
}

namespace {

struct ShippedPlugin {
  std::filesystem::path repository;
  std::filesystem::path plugin;
};

// A repository handed to the user that ships a native library and names it in
// its own metadata.
auto repository_shipping_a_plugin(const std::filesystem::path &root)
    -> ShippedPlugin {
  const auto repository_path = root / "repository";
  auto repository =
      liquid::runtime::block_on(liquid::repository::create(repository_path));
  EXPECT_TRUE(repository) << repository.error().message;
  const auto shipped = repository_path / "tools" /
                       std::filesystem::path(LIQUIFY_SAMPLE_PLUGIN).filename();
  std::filesystem::create_directories(shipped.parent_path());
  std::filesystem::copy_file(LIQUIFY_SAMPLE_PLUGIN, shipped);
  auto configured = liquid::runtime::block_on(repository->set_metadata(
      "config.liquify.plugins",
      text_bytes("tools/" + shipped.filename().string())));
  EXPECT_TRUE(configured) << configured.error().message;
  return {repository_path, shipped.lexically_normal()};
}

auto run_cli(const std::string &arguments, const std::filesystem::path &input,
             const std::filesystem::path &output) -> int {
  const auto command = "\"" + std::string(LIQUIFY_EXECUTABLE) + "\" " +
                       arguments + " < \"" + input.string() + "\" > \"" +
                       output.string() + "\" 2>&1";
  return std::system(command.c_str());
}

auto file_text(const std::filesystem::path &path) -> std::string {
  auto input = std::ifstream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

} // namespace

TEST(PluginTrust, RepositoryMetadataNamingAPluginLoadsNothing) {
  auto location = TemporaryDirectory("liquify-plugin-untrusted");
  const auto shipped = repository_shipping_a_plugin(location.path());
  const auto store = location.path() / "user" / "trusted-plugins";

  auto plugins = liquify::load_configured_plugins(
      shipped.repository, location.path() / "settings.liquid", store);
  ASSERT_TRUE(plugins) << plugins.error().message;
  EXPECT_TRUE(plugins->host->commands().empty());
  EXPECT_TRUE(plugins->host->query_names().empty());
  EXPECT_TRUE(plugins->host->panels(liquify::plugin_null_cbor()).empty());
  EXPECT_TRUE(plugins->failures.empty());
  ASSERT_EQ(plugins->untrusted.size(), 1);
  EXPECT_EQ(plugins->untrusted.front().path, shipped.plugin);
  EXPECT_EQ(plugins->untrusted.front().repository, shipped.repository);
  auto digest = liquify::plugin_digest(shipped.plugin);
  ASSERT_TRUE(digest) << digest.error().message;
  EXPECT_EQ(plugins->untrusted.front().digest, *digest);
  EXPECT_TRUE(digest->starts_with("sha256:"));
  EXPECT_FALSE(std::filesystem::exists(store.parent_path() / "plugin-cache"));
}

TEST(PluginTrust, ATrustStoreInsideTheRepositoryIsIgnored) {
  auto location = TemporaryDirectory("liquify-plugin-repository-store");
  const auto shipped = repository_shipping_a_plugin(location.path());
  auto planted = liquify::trust_plugin(
      shipped.plugin, shipped.repository / ".liquid" / "trusted-plugins");
  ASSERT_TRUE(planted) << planted.error().message;
  auto planted_settings =
      std::ofstream(shipped.repository / ".liquid" / "settings.liquid");
  planted_settings << "config.liquify.plugins=" << shipped.plugin.string()
                   << "\n";
  planted_settings.close();

  auto plugins = liquify::load_configured_plugins(
      shipped.repository, location.path() / "settings.liquid",
      location.path() / "user" / "trusted-plugins");
  ASSERT_TRUE(plugins) << plugins.error().message;
  EXPECT_TRUE(plugins->host->commands().empty());
  EXPECT_EQ(plugins->untrusted.size(), 1);
}

TEST(PluginTrust, ATrustedDigestLoadsFromTheUserCache) {
  auto location = TemporaryDirectory("liquify-plugin-trusted");
  const auto shipped = repository_shipping_a_plugin(location.path());
  const auto store = location.path() / "user" / "trusted-plugins";
  auto trusted = liquify::trust_plugin(shipped.plugin, store);
  ASSERT_TRUE(trusted) << trusted.error().message;
  auto again = liquify::trust_plugin(shipped.plugin, store);
  ASSERT_TRUE(again) << again.error().message;
  auto digests = liquify::trusted_plugin_digests(store);
  ASSERT_TRUE(digests) << digests.error().message;
  EXPECT_EQ(*digests, std::vector<std::string>{*trusted});

  auto plugins = liquify::load_configured_plugins(
      shipped.repository, location.path() / "settings.liquid", store);
  ASSERT_TRUE(plugins) << plugins.error().message;
  EXPECT_TRUE(plugins->untrusted.empty());
  EXPECT_TRUE(plugins->failures.empty());
  const auto hex = trusted->substr(std::string_view("sha256:").size());
  EXPECT_TRUE(std::filesystem::exists(
      store.parent_path() / "plugin-cache" /
      (hex + shipped.plugin.extension().string())));
  exercise_sample(plugins->host, location.path() / "state");
}

TEST(PluginTrust, ChangedBytesAfterTrustAreNotLoaded) {
  auto location = TemporaryDirectory("liquify-plugin-changed");
  const auto shipped = repository_shipping_a_plugin(location.path());
  const auto store = location.path() / "user" / "trusted-plugins";
  auto trusted = liquify::trust_plugin(shipped.plugin, store);
  ASSERT_TRUE(trusted) << trusted.error().message;
  {
    auto output = std::ofstream(shipped.plugin, std::ios::binary | std::ios::app);
    output << '\0';
  }

  auto plugins = liquify::load_configured_plugins(
      shipped.repository, location.path() / "settings.liquid", store);
  ASSERT_TRUE(plugins) << plugins.error().message;
  EXPECT_TRUE(plugins->host->commands().empty());
  ASSERT_EQ(plugins->untrusted.size(), 1);
  EXPECT_NE(plugins->untrusted.front().digest, *trusted);
}

TEST(PluginTrust, GlobalPluginsKeepLoadingBesideAnUntrustedRepository) {
  auto location = TemporaryDirectory("liquify-plugin-global-untrusted");
  const auto shipped = repository_shipping_a_plugin(location.path());
  const auto config_path = location.path() / "settings.liquid";
  auto config = std::ofstream(config_path);
  config << "config.liquify.plugins=" << LIQUIFY_SAMPLE_PLUGIN << "\n";
  config.close();

  auto plugins = liquify::load_configured_plugins(
      shipped.repository, config_path,
      location.path() / "user" / "trusted-plugins");
  ASSERT_TRUE(plugins) << plugins.error().message;
  EXPECT_TRUE(plugins->failures.empty());
  EXPECT_EQ(plugins->untrusted.size(), 1);
  exercise_sample(plugins->host, location.path() / "state");

  auto statuses = liquify::inspect_configured_plugins(
      shipped.repository, config_path,
      location.path() / "user" / "trusted-plugins");
  ASSERT_TRUE(statuses) << statuses.error().message;
  ASSERT_EQ(statuses->size(), 2);
  EXPECT_EQ(statuses->at(0).source, liquify::PluginSource::global);
  EXPECT_EQ(statuses->at(1).source, liquify::PluginSource::repository);
  EXPECT_FALSE(statuses->at(1).trusted);
}

TEST(PluginTrust, RejectsAMalformedTrustStore) {
  auto location = TemporaryDirectory("liquify-plugin-bad-store");
  const auto store = location.path() / "trusted-plugins";
  auto output = std::ofstream(store);
  output << "# comment\nblake3:" << std::string(64, '0') << "\n";
  output.close();
  auto digests = liquify::trusted_plugin_digests(store);
  ASSERT_FALSE(digests);
  EXPECT_EQ(digests.error().code, liquid::errc::invalid_argument);
}

TEST(PluginTrust, TheTrustCommandNamesThePluginAndRequiresConfirmation) {
  auto location = TemporaryDirectory("liquify-plugin-cli");
  const auto shipped = repository_shipping_a_plugin(location.path());
  const auto store = location.path() / "user" / "trusted-plugins";
  const auto answer = location.path() / "answer";
  const auto output = location.path() / "output";
  const auto store_option = " --trust-store \"" + store.string() + "\"";

  {
    auto file = std::ofstream(answer);
    file << "y\n";
  }
  EXPECT_NE(run_cli("plugin trust \"" + shipped.plugin.string() + "\"" +
                        store_option,
                    answer, output),
            0);
  auto prompt = file_text(output);
  EXPECT_NE(prompt.find(shipped.plugin.filename().string()), std::string::npos)
      << prompt;
  EXPECT_NE(prompt.find(shipped.plugin.parent_path().string()),
            std::string::npos)
      << prompt;
  EXPECT_NE(prompt.find("sha256:"), std::string::npos) << prompt;
  EXPECT_FALSE(std::filesystem::exists(store));

  EXPECT_EQ(run_cli("plugin list \"" + shipped.repository.string() + "\"" +
                        store_option,
                    answer, output),
            0);
  EXPECT_NE(file_text(output).find("not trusted"), std::string::npos)
      << file_text(output);

  {
    auto file = std::ofstream(answer);
    file << "yes\n";
  }
  EXPECT_EQ(run_cli("plugin trust \"" + shipped.plugin.string() + "\"" +
                        store_option,
                    answer, output),
            0)
      << file_text(output);
  auto digests = liquify::trusted_plugin_digests(store);
  ASSERT_TRUE(digests) << digests.error().message;
  EXPECT_EQ(digests->size(), 1);

  EXPECT_EQ(run_cli("plugin list \"" + shipped.repository.string() + "\"" +
                        store_option,
                    answer, output),
            0);
  EXPECT_NE(file_text(output).find("trusted"), std::string::npos);
  EXPECT_EQ(file_text(output).find("not trusted"), std::string::npos)
      << file_text(output);
}

// liquify#149: the dispatcher sends panels, commands and events from a worker, so the UI
// thread's calls return at once whatever the plugin does.
namespace {

using namespace std::chrono_literals;

// A static plugin whose panel and command block until the test opens the gate.
std::atomic_bool gate_open{};
std::atomic_int gate_calls{};

void wait_for_gate() {
  ++gate_calls;
  while (!gate_open.load()) {
    std::this_thread::sleep_for(1ms);
  }
}

auto gated_panel(void *context, liquid_frontend_bytes_v1 input,
                 const liquid_frontend_callbacks_v1 *callbacks,
                 liquid_frontend_owned_bytes_v1 *output,
                 liquid_frontend_error_v1 *error) -> int {
  wait_for_gate();
  return healthy_panel(context, input, callbacks, output, error);
}

auto gated_command(void *, liquid_frontend_bytes_v1,
                   const liquid_frontend_callbacks_v1 *,
                   liquid_frontend_owned_bytes_v1 *output,
                   liquid_frontend_error_v1 *) -> int {
  wait_for_gate();
  static constexpr std::uint8_t done[] = {0xf5};
  return copy_result(done, output);
}

constexpr std::uint8_t gated_schema[] = {0xa0};
const liquify_plugin_command_v1 gated_commands[] = {
    {"gated.run", "", {gated_schema, sizeof(gated_schema)}, gated_command}};
const liquify_plugin_panel_v1 gated_panels[] = {{"gated.view", gated_panel}};
const liquify_plugin_v1 gated_descriptor = {LIQUIFY_PLUGIN_ABI_VERSION,
                                            sizeof(liquify_plugin_v1),
                                            "gated",
                                            nullptr,
                                            0,
                                            gated_commands,
                                            1,
                                            gated_panels,
                                            1,
                                            nullptr,
                                            nullptr};

auto gated_host() -> std::shared_ptr<liquify::PluginHost> {
  gate_open = false;
  gate_calls = 0;
  auto host = std::make_shared<liquify::PluginHost>();
  auto added = host->add(&gated_descriptor);
  EXPECT_TRUE(added) << added.error().message;
  return host;
}

// Waits on the test thread (never the UI's) for a condition the workers make true.
template <class Condition>
auto eventually(Condition condition, std::chrono::milliseconds limit) -> bool {
  const auto until = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < until) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return condition();
}

// How long one call on the caller's thread took.
template <class Call> auto elapsed(Call call) -> std::chrono::milliseconds {
  const auto start = std::chrono::steady_clock::now();
  call();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
}

// Far below the 2 s panel deadline: a call that waited on the plugin could not meet it.
constexpr auto no_wait = 250ms;

} // namespace

TEST(PluginDispatcher, PanelsAreLoadingUntilTheyAnswerAndTheRefreshNeverWaits) {
  auto host = gated_host();
  auto dispatcher = liquify::PluginDispatcher(host);
  auto panels = dispatcher.panels();
  ASSERT_EQ(panels.size(), 1);
  EXPECT_EQ(panels[0].plugin, "gated");
  EXPECT_EQ(panels[0].name, "gated.view");
  EXPECT_TRUE(panels[0].loading);

  EXPECT_LT(elapsed([&] { dispatcher.refresh_panels(liquify::plugin_null_cbor()); }),
            no_wait);
  ASSERT_TRUE(eventually([] { return gate_calls.load() == 1; }, 5s));
  EXPECT_LT(elapsed([&] { panels = dispatcher.panels(); }), no_wait);
  EXPECT_TRUE(panels[0].loading);

  // A refresh while one is still being asked is not queued behind it.
  dispatcher.refresh_panels(liquify::plugin_null_cbor());
  EXPECT_EQ(dispatcher.pending(), 1);

  const auto before = dispatcher.generation();
  gate_open = true;
  ASSERT_TRUE(eventually([&] { return dispatcher.generation() != before; }, 5s));
  panels = dispatcher.panels();
  ASSERT_EQ(panels.size(), 1);
  EXPECT_FALSE(panels[0].loading);
  EXPECT_TRUE(panels[0].error.empty()) << panels[0].error;
  auto model = liquify::decode_plugin_text_panel(panels[0].view_model_cbor);
  ASSERT_TRUE(model) << model.error().message;
  EXPECT_EQ(model->title, "Healthy");
  EXPECT_EQ(gate_calls.load(), 1);
}

TEST(PluginDispatcher, APanelKeepsItsLastAnswerWhileTheNextRefreshIsAsked) {
  auto host = gated_host();
  gate_open = true;
  auto dispatcher = liquify::PluginDispatcher(host);
  dispatcher.refresh_panels(liquify::plugin_null_cbor());
  ASSERT_TRUE(eventually([&] { return !dispatcher.panels()[0].loading; }, 5s));

  gate_open = false;
  gate_calls = 0;
  dispatcher.refresh_panels(liquify::plugin_null_cbor());
  ASSERT_TRUE(eventually([] { return gate_calls.load() == 1; }, 5s));
  const auto panels = dispatcher.panels();
  EXPECT_FALSE(panels[0].loading);
  auto model = liquify::decode_plugin_text_panel(panels[0].view_model_cbor);
  ASSERT_TRUE(model) << model.error().message;
  EXPECT_EQ(model->title, "Healthy");
  gate_open = true;
  ASSERT_TRUE(eventually([&] { return dispatcher.pending() == 0; }, 5s));
}

TEST(PluginDispatcher, ACommandRunsOnAWorkerAndReportsItsOutcome) {
  auto host = gated_host();
  auto answers = std::make_shared<std::atomic_int>(0);
  auto dispatcher = liquify::PluginDispatcher(host, [answers] { ++*answers; });
  auto invoked = liquid::result<void>{};
  EXPECT_LT(elapsed([&] {
              invoked = dispatcher.invoke_command("gated.run", liquify::plugin_empty_map_cbor());
            }),
            no_wait);
  ASSERT_TRUE(invoked) << invoked.error().message;
  ASSERT_TRUE(eventually([] { return gate_calls.load() == 1; }, 5s));
  EXPECT_EQ(dispatcher.running_commands(), (std::vector<std::string>{"gated.run"}));
  EXPECT_TRUE(dispatcher.take_command_outcomes().empty());

  gate_open = true;
  ASSERT_TRUE(eventually([&] { return dispatcher.running_commands().empty(); }, 5s));
  const auto outcomes = dispatcher.take_command_outcomes();
  ASSERT_EQ(outcomes.size(), 1);
  EXPECT_EQ(outcomes[0].name, "gated.run");
  EXPECT_TRUE(outcomes[0].error.empty()) << outcomes[0].error;
  EXPECT_EQ(outcomes[0].result_cbor, std::vector<std::byte>{std::byte{0xf5}});
  EXPECT_EQ(answers->load(), 1);
  EXPECT_TRUE(dispatcher.take_command_outcomes().empty());
}

TEST(PluginDispatcher, AnUnregisteredCommandIsNotFound) {
  auto host = gated_host();
  auto dispatcher = liquify::PluginDispatcher(host);
  auto invoked = dispatcher.invoke_command("missing.command", liquify::plugin_empty_map_cbor());
  ASSERT_FALSE(invoked);
  EXPECT_EQ(invoked.error().code, liquid::errc::not_found);
  EXPECT_EQ(invoked.error().context, "missing.command");
  EXPECT_TRUE(dispatcher.running_commands().empty());
}

TEST(PluginDispatcher, DestroyingItNeverWaitsForAPluginStillRunning) {
  auto host = gated_host();
  auto answers = std::make_shared<std::atomic_int>(0);
  {
    auto dispatcher = std::make_unique<liquify::PluginDispatcher>(host, [answers] { ++*answers; });
    ASSERT_TRUE(dispatcher->invoke_command("gated.run", liquify::plugin_empty_map_cbor()));
    ASSERT_TRUE(eventually([] { return gate_calls.load() == 1; }, 5s));
    EXPECT_LT(elapsed([&] { dispatcher.reset(); }), no_wait);
  }
  gate_open = true;
  // The worker finishes the call, drops the answer and exits; the host outlives it.
  ASSERT_TRUE(eventually([&] { return host.use_count() == 1; }, 5s));
  EXPECT_EQ(answers->load(), 0);
}

TEST(PluginDispatcher, EventsAreDeliveredOnTheWorker) {
  auto state = EventState{};
  const auto descriptor = liquify_plugin_v1{LIQUIFY_PLUGIN_ABI_VERSION,
                                            sizeof(liquify_plugin_v1),
                                            "events",
                                            nullptr,
                                            0,
                                            nullptr,
                                            0,
                                            nullptr,
                                            0,
                                            &state,
                                            nullptr,
                                            &event_callbacks};
  auto host = std::make_shared<liquify::PluginHost>();
  ASSERT_TRUE(host->add(&descriptor));
  auto dispatcher = liquify::PluginDispatcher(host);
  auto presence = liquid::presence{};
  presence.actor.taap_id = "person";
  presence.path = "main.cpp";
  presence.activity = liquid::presence_activity::editing;
  dispatcher.publish_presence(presence);
  auto lease = liquid::semantic_lease{};
  lease.by.taap_id = "person";
  lease.claim.path = "main.cpp";
  lease.claim.kind = liquid::lease_kind::file;
  dispatcher.publish_lease(lease);
  auto attribution = liquid::line_attribution{};
  attribution.line = 1;
  attribution.by.taap_id = "agent";
  attribution.agent = true;
  dispatcher.publish_attribution("main.cpp", liquid::change_id{},
                                 std::span{&attribution, std::size_t{1}});
  ASSERT_TRUE(eventually([&] { return dispatcher.pending() == 0; }, 5s));
  EXPECT_EQ(state.presence_count, 1);
  EXPECT_EQ(state.lease_count, 1);
  EXPECT_EQ(state.attribution_count, 1);
}

// The fault plugin's hang case, in its own host process: the deadline and restart budget are
// the host's, unchanged; only the waiting moved off the caller's thread.
TEST(PluginDispatcher, AHangingDynamicPanelAndCommandNeverBlockTheCaller) {
  auto host = load_fault_plugin();
  auto dispatcher = liquify::PluginDispatcher(host);
  ASSERT_EQ(dispatcher.panels().size(), 2);
  EXPECT_LT(elapsed([&] { dispatcher.refresh_panels(liquify::plugin_null_cbor()); }),
            no_wait);
  auto invoked = liquid::result<void>{};
  EXPECT_LT(elapsed([&] {
              invoked = dispatcher.invoke_command("fault.sleep", liquify::plugin_empty_map_cbor());
            }),
            no_wait);
  ASSERT_TRUE(invoked) << invoked.error().message;
  EXPECT_LT(elapsed([&] { (void)dispatcher.panels(); }), no_wait);
  EXPECT_TRUE(dispatcher.panels()[0].loading);

  // The hanging panel misses its 2 s deadline and the healthy one answers after it.
  ASSERT_TRUE(eventually([&] { return !dispatcher.panels()[1].loading; }, 8s));
  const auto panels = dispatcher.panels();
  EXPECT_NE(panels[0].error.find("did not answer in time"), std::string::npos)
      << panels[0].error;
  ASSERT_TRUE(panels[1].error.empty()) << panels[1].error;

  // The hanging command misses its 10 s deadline, queued behind the panels.
  ASSERT_TRUE(eventually([&] { return dispatcher.running_commands().empty(); }, 20s));
  const auto outcomes = dispatcher.take_command_outcomes();
  ASSERT_EQ(outcomes.size(), 1);
  EXPECT_EQ(outcomes[0].name, "fault.sleep");
  EXPECT_NE(outcomes[0].error.find("did not answer in time"), std::string::npos)
      << outcomes[0].error;
}
