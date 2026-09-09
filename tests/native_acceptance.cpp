#include "native_acceptance.hpp"

#include "../src/editor_buffer.hpp"
#include "../src/repository_view.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/editor_graph.hpp"
#include "ui_property_assert.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>

namespace {

void require(bool value, std::string_view message) {
  liquify::test::require_property(value, std::string(message));
  std::printf("PASS: %.*s\n", static_cast<int>(message.size()), message.data());
}

const std::string source = "// Native acceptance repository\n"
                           "int answer() {\n"
                           "  return 42;\n"
                           "}\n";

void create_repository(const std::filesystem::path &path) {
  auto repo = liquid::runtime::block_on(liquid::repository::create(path));
  require(repo.has_value(),
          "disposable repository created by pinned libliquid");
  for (const auto name : {"main.cpp", "other.cpp"}) {
    const auto bytes = std::as_bytes(std::span(source));
    const auto written = liquid::runtime::block_on(
        repo->write(name, {bytes.begin(), bytes.end()}));
    require(written.has_value(), std::string("persisted ") + name);
  }
}

std::string read_back(const std::filesystem::path &path) {
  auto repo = liquid::runtime::block_on(liquid::repository::open(path));
  require(repo.has_value(), "independent repository handle reopened");
  const auto bytes = liquid::runtime::block_on(repo->read("main.cpp"));
  require(bytes.has_value(), "persisted main.cpp blob read");
  return {reinterpret_cast<const char *>(bytes->data()), bytes->size()};
}

void save_image(const std::filesystem::path &path,
                const liquify::ScreenshotImage &image) {
  require(liquify::write_png(path, image).has_value(),
          "live framebuffer evidence saved");
}

} // namespace

namespace liquify {

void NativeAcceptance::open_file(App &app) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    pulse(app);
    if (app.frame_layout().controls.contains("repository/main.cpp") &&
        app.repository_ && !app.repository_->navigation_loading()) {
      break;
    }
  }
  require(app.frame_layout().controls.contains("repository/main.cpp"),
          "real repository row appeared");
  click(app, app.frame_layout().controls.at("repository/main.cpp"));
  require(app.editor_ && app.editor_->display_path() == "main.cpp" &&
              app.editor_->text() == source,
          "SDL pointer opened main.cpp with persisted source bytes");
  source_tab(app);
  require(app.editor_visible_, "source tab exposes retained editor");
}

void NativeAcceptance::geometry(App &app, const std::filesystem::path &output) {
  auto image = capture(app);
  const auto coverage =
      test::require_window_coverage(image, app.frame_layout());
  std::printf("launch: scale=%.1fx%.1f logical=%.0fx%.0f drawable=%ux%u "
              "coverage=%.3f\n",
              app.frame_layout().drawable_scale.x,
              app.frame_layout().drawable_scale.y,
              app.frame_layout().window.width, app.frame_layout().window.height,
              image.width, image.height, coverage);
  save_image(output / "launch.png", image);
  open_file(app);
  image = capture(app);
  test::require_window_coverage(image, app.frame_layout());
  test::require_toolbar(image, app.frame_layout());
  test::require_editor(image, app.frame_layout());
  fonts(app);
  save_image(output / "editor.png", image);
  resize(app);
  image = capture(app);
  test::require_window_coverage(image, app.frame_layout());
  test::require_toolbar(image, app.frame_layout());
  test::require_editor(image, app.frame_layout());
  save_image(output / "resized.png", image);
  key(app, "p", true, true);
  require(popup("Command palette"), "keyboard opened modal over real source");
  save_image(output / "palette.png", capture(app));
}

void NativeAcceptance::modal_input(App &app) {
  open_file(app);
  click(app, app.frame_layout().editor);
  const auto before = std::string(app.editor_->text());
  key(app, "p", true, true);
  require(popup("Command palette"), "palette opened over focused source");
  text(app, "modal query");
  require(app.command_palette_query_ == "modal query",
          "modal query received native text");
  std::printf("modal: source_before_bytes=%zu source_after_bytes=%zu\n",
              before.size(), app.editor_->text().size());
  require(app.editor_->text() == before,
          "#39: modal text does not edit obscured source");
}

void NativeAcceptance::quick_open(App &app) {
  open_file(app);
  key(app, "p", true);
  require(popup("Quick open"), "Primary+P opened Quick Open");
  text(app, "other.cpp");
  std::printf("quick-open: query='%s' expected='other.cpp'\n",
              app.quick_open_query_.c_str());
  require(app.quick_open_query_ == "other.cpp",
          "#33: Quick Open receives keyboard query");
  key(app, "Down");
  key(app, "Enter");
  require(app.editor_ && app.editor_->display_path() == "other.cpp" &&
              !popup("Quick open"),
          "#33: Down/Enter opens selected result and closes Quick Open");
}

void NativeAcceptance::edit(App &app, const std::filesystem::path &output) {
  open_file(app);
  key(app, "p", true);
  require(popup("Quick open"), "Quick Open opened before focus transition");
  click(app, app.frame_layout().controls.at("quick-open/query"));
  text(app, "main.cpp");
  require(app.quick_open_query_ == "main.cpp",
          "SDL text reached focused ImGui field");
  click(app, app.frame_layout().controls.at("quick-open/main.cpp"));
  require(!popup("Quick open"), "SDL pointer selected Quick Open result");
  source_tab(app);
  click(app, app.frame_layout().editor);
  const auto before = std::string(app.editor_->text());
  const auto cursor = app.editor_->cursor();
  std::size_t offset = cursor.byte;
  for (std::size_t line = 0; line < cursor.line; ++line) {
    offset += app.editor_->line(line).size() + 1;
  }
  const std::string marker = "NATIVE_56_UTF8_é";
  auto expected = before;
  expected.insert(offset, marker);
  text(app, marker);
  std::printf("type: path=main.cpp cursor=%zu:%zu expected_bytes=%zu "
              "actual_bytes=%zu dirty=%d\n",
              cursor.line, cursor.byte, expected.size(),
              app.editor_->text().size(), app.editor_->dirty());
  save_image(output / "after-input.png", capture(app));
  key(app, "s", true);
  const auto actual = read_back(*app.options_.repository_path);
  std::printf("save/read-back: expected_bytes=%zu persisted_bytes=%zu "
              "byte_comparison=%s\n",
              expected.size(), actual.size(),
              expected == actual ? "equal" : "DIFFERENT");
  require(actual == expected,
          "#39: typing after ImGui focus persists exactly through Save");
}

void NativeAcceptance::palette(App &app) {
  open_file(app);
  const auto before = app.shell_state_.bottom_panel_visible;
  key(app, "p", true, true);
  require(popup("Command palette"), "Primary+Shift+P opened palette");
  text(app, "Toggle bottom panel");
  require(app.command_palette_query_ == "Toggle bottom panel",
          "palette receives keyboard query");
  key(app, "Down");
  key(app, "Enter");
  std::printf("palette: visible=%d bottom_before=%d bottom_after=%d\n",
              popup("Command palette"), before,
              app.shell_state_.bottom_panel_visible);
  require(!popup("Command palette") &&
              app.shell_state_.bottom_panel_visible != before,
          "palette Down/Enter dispatches selected command");
}

void NativeAcceptance::tabs(App &app) {
  open_file(app);
  key(app, "j", true);
  if (!app.shell_state_.bottom_panel_visible) {
    key(app, "j", true);
  }
  require(app.shell_state_.bottom_panel_visible,
          "Primary+J exposed bottom tabs");
  auto failed = std::string{};
  const auto visible = app.frame_layout().panels;
  std::size_t count = 0;
  for (const auto &[id, geometry] : visible) {
    if (geometry.tab.width <= 0 || geometry.tab.height <= 0) {
      continue;
    }
    const auto name = std::string(panel_id_name(id));
    const auto tab = app.frame_layout().panels.at(id).tab;
    test::require_bounds(tab, app.frame_layout().window, name);
    click(app, tab);
    const auto &current = app.frame_layout().panels.at(id);
    const auto selected = current.selected && current.content.height > 0;
    std::printf("tab: %s selected=%d content=%.0fx%.0f\n", name.c_str(),
                selected, current.content.width, current.content.height);
    if (!selected) {
      failed += name + " ";
    }
    ++count;
  }
  require(count > 1, "multiple visible panel tabs received SDL pointer events");
  require(failed.empty(),
          "#35: every visible tab selects its panel; failed=" + failed);
}

void NativeAcceptance::build(App &app) {
  open_file(app);
  require(app.editor_graph_ != nullptr, "real repository has editor graph");
  app.editor_graph_->wait_until_idle();
  key(app, "F7");
  app.editor_graph_->wait_until_idle();
  pulse(app);
  const auto snapshot = app.editor_graph_->snapshot();
  std::printf("build: running=%d actions=%zu result='%s' error='%s'\n",
              snapshot.build_running, snapshot.build_actions.size(),
              snapshot.build_result.c_str(), snapshot.query_error.c_str());
  require(!snapshot.build_running && !snapshot.query_error.empty() &&
              snapshot.build_result.empty(),
          "unconfigured build reports unavailable without fabricated success");
  key(app, "F9");
  app.editor_graph_->wait_until_idle();
  pulse(app);
  const auto deploy = app.editor_graph_->snapshot().deploy;
  std::printf("offline: running=%d objects=%llu error='%s'\n", deploy.running,
              static_cast<unsigned long long>(deploy.objects),
              deploy.error.c_str());
  require(!deploy.running && !deploy.error.empty() && deploy.objects == 0,
          "missing remote reports offline without fabricated upload");
}

int NativeAcceptance::run(std::string_view scenario,
                          const std::filesystem::path &output) {
  std::filesystem::create_directories(output);
  const auto repo =
      output /
      ("repository-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  create_repository(repo);
  App app({.repository_path = repo});
  const auto launch_status = launch(app, scenario == "launch-1" ? 1 : 2);
  if (launch_status != 0) {
    return launch_status;
  }
  require(true, "real native application window initialized");
  pulse(app);
  if (scenario.starts_with("launch-")) {
    geometry(app, output);
  } else if (scenario == "edit") {
    edit(app, output);
  } else if (scenario == "quick-open") {
    quick_open(app);
  } else if (scenario == "modal-input") {
    modal_input(app);
  } else if (scenario == "palette") {
    palette(app);
  } else if (scenario == "tabs") {
    tabs(app);
  } else if (scenario == "build") {
    build(app);
  } else {
    throw std::runtime_error("unknown native acceptance scenario");
  }
  std::printf("native acceptance: PASS %.*s\n",
              static_cast<int>(scenario.size()), scenario.data());
  return 0;
}

} // namespace liquify

int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc != 3) {
    std::fprintf(
        stderr, "usage: liquify-native-acceptance SCENARIO OUTPUT_DIRECTORY\n");
    return 2;
  }
  try {
    return liquify::NativeAcceptance::run(argv[1],
                                          std::filesystem::absolute(argv[2]));
  } catch (const std::exception &error) {
    std::fprintf(stderr, "native acceptance: FAIL %s\n", error.what());
    return 1;
  }
}
