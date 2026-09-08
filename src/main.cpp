#include <charconv>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "liquid/liquid.hpp"
#include "liquify/app.hpp"
#include "liquify/command.hpp"
#include "liquify/interactive_analysis.hpp"
#include "liquify/plugin.hpp"
#include "liquify/screenshot.hpp"
#include "liquify/structural_edit.hpp"
#include "liquify/version.hpp"
#include "repository_view.hpp"

namespace {
void usage() {
  std::println("{}", liquify::build_description());
  std::println("  liquify [PATH]              open the IDE, optionally on a "
               "liquid repository");
  std::println("  liquify --lake URL PATH     open repository reviews from a "
               "lake forge");
  std::println(
      "  liquify --print-tree PATH   print a repository root tree and exit");
  std::println("  liquify --apply-edits FILE REPOSITORY");
  std::println("                              apply structural-edit CBOR and "
               "return result CBOR");
  std::println("  liquify --smoke [N]         scroll 50,000 lines for N frames "
               "and exit");
  std::println("  liquify --screenshot STATE --out PNG");
  std::println(
      "                              render a fixed headless UI state");
  std::println(
      "  liquify --command NAME      run a registered headless command");
  std::println("  liquify docs commands|keymap [--out FILE|--check FILE]");
  std::println(
      "                              generate or verify command documentation");
  std::println("  liquify version             print the version");
}

auto parse_frame_count(std::string_view argument, int &count) -> bool {
  const auto *begin = argument.data();
  const auto *end = begin + argument.size();
  const auto result = std::from_chars(begin, end, count);
  return result.ec == std::errc{} && result.ptr == end && count > 0;
}

auto print_tree(const std::filesystem::path &path) -> int {
  const auto view = liquify::RepositoryView::open(path);
  if (!view.error().empty()) {
    std::println(stderr, "liquify: {}", view.error());
    return 1;
  }

  std::println("name\tkind\tmode");
  for (const auto &entry : view.entries()) {
    std::println("{}\t{}\t{:06o}", entry.name,
                 liquify::node_kind_name(entry.kind), entry.mode);
  }
  return 0;
}

auto apply_edits(const std::filesystem::path &request_path,
                 const std::filesystem::path &repository_path) -> int {
  auto input = std::ifstream(request_path, std::ios::binary);
  if (!input) {
    std::println(stderr, "liquify: cannot read {}", request_path.string());
    return 1;
  }
  const auto characters =
      std::vector<char>(std::istreambuf_iterator<char>(input), {});
  const auto request =
      std::as_bytes(std::span(characters.data(), characters.size()));
  auto edits = liquify::decode_structural_edit_request(request);
  if (!edits) {
    std::println(stderr, "liquify: {}", edits.error().message);
    return 1;
  }
  auto analysis = liquify::InteractiveAnalysis::open(
      repository_path / ".liquid" / "liquify-structural-graph");
  if (!analysis) {
    std::println(stderr, "liquify: {}", analysis.error().message);
    return 1;
  }
  auto editor = liquify::StructuralEditor(repository_path, *analysis);
  auto result = editor.apply(*edits);
  if (!result) {
    std::println(stderr, "liquify: {}", result.error().message);
    return 1;
  }
  auto encoded = liquify::encode_structural_edit_result(*result);
  if (!encoded) {
    std::println(stderr, "liquify: {}", encoded.error().message);
    return 1;
  }
  const auto written = std::fwrite(encoded->data(), 1, encoded->size(), stdout);
  return written == encoded->size() ? 0 : 1;
}

auto command_registry(const std::shared_ptr<liquify::PluginHost> &plugins)
    -> std::expected<liquify::CommandRegistry, std::string> {
  auto registry = liquify::CommandRegistry{};
  for (const auto &command : liquify::builtin_commands()) {
    auto added = registry.add(command);
    if (!added) {
      return std::unexpected(added.error());
    }
  }
  if (plugins) {
    for (const auto &plugin : plugins->commands()) {
      auto bindings = std::vector<std::string>{};
      if (!plugin.default_binding.empty()) {
        bindings.push_back(plugin.default_binding);
      }
      auto added = registry.add(
          {plugin.name, plugin.name, "Plugins",
           "Command registered by the " + plugin.plugin + " plugin.",
           std::move(bindings), true, true});
      if (!added) {
        return std::unexpected(added.error());
      }
    }
  }
  return registry;
}

auto run_named_command(std::string_view name) -> int {
  auto configured = liquify::load_global_plugins();
  if (!configured) {
    std::println(stderr, "liquify: {}: {}", configured.error().message,
                 configured.error().context);
    return 1;
  }
  auto registry = command_registry(configured->host);
  if (!registry) {
    std::println(stderr, "liquify: {}", registry.error());
    return 1;
  }
  const auto *command = registry->find(name);
  if (command == nullptr) {
    std::println(stderr, "liquify: command is not registered: {}", name);
    return 2;
  }
  if (command->plugin) {
    auto result = configured->host->invoke_command(
        name, liquify::plugin_empty_map_cbor());
    if (!result) {
      std::println(stderr, "liquify: {}: {}", result.error().message,
                   result.error().context);
      return 1;
    }
    const auto written = std::fwrite(result->data(), 1, result->size(), stdout);
    return written == result->size() ? 0 : 1;
  }
  if (name == "help.version") {
    std::println("{}", liquify::build_description());
    return 0;
  }
  if (name == "help.command-reference") {
    std::print("{}", liquify::generate_command_reference());
    return 0;
  }
  std::println(stderr, "liquify: command requires an open IDE: {}", name);
  return 1;
}

auto command_docs(int argc, char **argv, std::string_view topic) -> int {
  if (argc != 3 && argc != 5) {
    std::println(stderr, "liquify: docs {} accepts --out FILE or --check FILE",
                 topic);
    return 2;
  }
  const auto generated = topic == "commands"
                             ? liquify::generate_command_reference()
                             : liquify::generate_default_keymap();
  if (argc == 3) {
    std::print("{}", generated);
    return 0;
  }
  const auto option = std::string_view(argv[3]);
  const auto path = std::filesystem::path(argv[4]);
  if (option == "--out") {
    auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
    output << generated;
    if (!output) {
      std::println(stderr, "liquify: cannot write {}", path.string());
      return 1;
    }
    return 0;
  }
  if (option == "--check") {
    auto input = std::ifstream(path, std::ios::binary);
    const auto existing =
        std::string(std::istreambuf_iterator<char>(input), {});
    if (!input || existing != generated) {
      std::println(stderr,
                   "liquify: generated file is stale: run docs {} --out {}",
                   topic, path.string());
      return 1;
    }
    return 0;
  }
  std::println(stderr, "liquify: docs {} accepts --out or --check", topic);
  return 2;
}
} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--apply-edits") {
    if (argc != 4) {
      std::println(
          stderr, "liquify: --apply-edits requires a CBOR file and repository");
      return 2;
    }
    return apply_edits(argv[2], argv[3]);
  }
  if (argc > 1 && std::string_view(argv[1]) == "--command") {
    if (argc != 3) {
      std::println(stderr, "liquify: --command requires one command name");
      return 2;
    }
    return run_named_command(argv[2]);
  }
  if (argc > 1 && std::string_view(argv[1]) == "docs") {
    if (argc < 3 || (std::string_view(argv[2]) != "commands" &&
                     std::string_view(argv[2]) != "keymap")) {
      std::println(stderr, "liquify: unknown docs topic");
      return 2;
    }
    return command_docs(argc, argv, argv[2]);
  }
  liquify::AppOptions opts;
  opts.executable_path = argv[0];
  bool should_print_tree = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "version" || arg == "--version") {
      if (argc != 2) {
        std::println(stderr, "liquify: version accepts no other arguments");
        return 2;
      }
      std::println("{}", liquify::build_description());
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    }
    if (arg == "--smoke") {
      opts.smoke = true;
      opts.max_frames = 30;
      if (i + 1 < argc) {
        int frames = 0;
        if (parse_frame_count(argv[i + 1], frames)) {
          opts.max_frames = frames;
          ++i;
        }
      }
      continue;
    }
    if (arg == "--screenshot") {
      if (i + 1 == argc) {
        std::println(stderr, "liquify: --screenshot requires a state");
        return 2;
      }
      if (opts.screenshot_state) {
        std::println(stderr, "liquify: --screenshot may be specified once");
        return 2;
      }
      opts.screenshot_state = std::string{argv[++i]};
      opts.max_frames = 2;
      continue;
    }
    if (arg == "--out") {
      if (i + 1 == argc) {
        std::println(stderr, "liquify: --out requires a PNG path");
        return 2;
      }
      if (opts.screenshot_output) {
        std::println(stderr, "liquify: --out may be specified once");
        return 2;
      }
      opts.screenshot_output = std::filesystem::path(argv[++i]);
      continue;
    }
    if (arg == "--print-tree") {
      if (i + 1 == argc) {
        std::println(stderr,
                     "liquify: --print-tree requires a repository path");
        return 2;
      }
      if (opts.repository_path) {
        std::println(stderr, "liquify: accepts at most one repository path");
        return 2;
      }
      should_print_tree = true;
      opts.repository_path = std::filesystem::path(argv[++i]);
      continue;
    }
    if (arg == "--lake") {
      if (i + 1 == argc) {
        std::println(stderr, "liquify: --lake requires a repository URL");
        return 2;
      }
      if (opts.lake_url) {
        std::println(stderr, "liquify: --lake may be specified once");
        return 2;
      }
      opts.lake_url = std::string{argv[++i]};
      continue;
    }
    if (arg.starts_with("-")) {
      std::println(stderr, "liquify: unknown option {}", arg);
      usage();
      return 2;
    }
    if (opts.repository_path) {
      std::println(stderr, "liquify: accepts at most one repository path");
      return 2;
    }
    opts.repository_path = std::filesystem::path(arg);
  }
  if (opts.max_frames > 0 && opts.repository_path) {
    std::println(stderr, "liquify: headless modes do not open a repository");
    return 2;
  }
  if (opts.lake_url && !opts.repository_path) {
    std::println(stderr, "liquify: --lake requires a repository path");
    return 2;
  }
  if (opts.screenshot_state) {
    if (liquify::find_screenshot_state(*opts.screenshot_state) == nullptr) {
      std::println(stderr, "liquify: unknown screenshot state {}",
                   *opts.screenshot_state);
      std::print(stderr, "liquify: states:");
      for (const auto &state : liquify::screenshot_states()) {
        std::print(stderr, " {}", state.name);
      }
      std::println(stderr);
      return 2;
    }
    if (!opts.screenshot_output) {
      std::println(stderr, "liquify: --screenshot requires --out");
      return 2;
    }
    if (opts.repository_path || opts.smoke || opts.lake_url) {
      std::println(
          stderr,
          "liquify: --screenshot cannot open a repository or run smoke");
      return 2;
    }
  } else if (opts.screenshot_output) {
    std::println(stderr, "liquify: --out requires --screenshot");
    return 2;
  }
  if (should_print_tree) {
    return print_tree(*opts.repository_path);
  }
  liquify::App app(opts);
  if (!app.init())
    return 1;
  return app.run();
}
