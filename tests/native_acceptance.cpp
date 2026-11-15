#include "native_acceptance.hpp"

#include "../src/editor_buffer.hpp"
#include "../src/repository_view.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/editor_graph.hpp"
#include "ui_property_assert.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <array>
#include <format>
#include <fstream>
#include <optional>
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

// A loopback HTTP responder that answers every request with one canned status, standing in for
// a lake that answers. Test code only: liquify itself reaches lakes through libliquid.
class CannedLake {
public:
  explicit CannedLake(int status) : status_(status) {
    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    require(socket_ >= 0, "canned lake socket created");
    auto address = sockaddr_in{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(::bind(socket_, reinterpret_cast<sockaddr *>(&address),
                   sizeof(address)) == 0 &&
                ::listen(socket_, 8) == 0,
            "canned lake listening on loopback");
    auto length = socklen_t{sizeof(address)};
    ::getsockname(socket_, reinterpret_cast<sockaddr *>(&address), &length);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { serve(); });
  }

  ~CannedLake() {
    stopping_ = true;
    ::shutdown(socket_, SHUT_RDWR);
    ::close(socket_);
    thread_.join();
  }

  CannedLake(const CannedLake &) = delete;
  auto operator=(const CannedLake &) -> CannedLake & = delete;

  [[nodiscard]] auto port() const -> int { return port_; }
  [[nodiscard]] auto requests() const -> int { return requests_; }

private:
  void serve() {
    while (!stopping_) {
      const auto client = ::accept(socket_, nullptr, nullptr);
      if (client < 0) {
        return;
      }
      auto timeout = timeval{.tv_sec = 2, .tv_usec = 0};
      ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      auto request = std::string{};
      auto buffer = std::array<char, 4096>{};
      while (request.find("\r\n\r\n") == std::string::npos) {
        const auto received = ::recv(client, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
          break;
        }
        request.append(buffer.data(), static_cast<std::size_t>(received));
      }
      ++requests_;
      const auto response =
          "HTTP/1.1 " + std::to_string(status_) +
          " Canned\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      ::send(client, response.data(), response.size(), 0);
      ::close(client);
    }
  }

  int status_;
  int socket_{-1};
  int port_{};
  std::atomic_bool stopping_{};
  std::atomic_int requests_{};
  std::thread thread_;
};

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

void NativeAcceptance::build(App &app, const std::filesystem::path &output) {
  open_file(app);
  require(app.editor_graph_ != nullptr, "real repository has editor graph");
  app.editor_graph_->wait_until_idle();
  key(app, "F7");
  app.editor_graph_->wait_until_idle();
  pulse(app);
  auto snapshot = app.editor_graph_->snapshot();
  const auto repository = app.repository_->path().string();
  const auto build_failure = describe_build_failure(
      snapshot.build_error.value_or(liquid::error{}), app.repository_->path());
  std::printf("build: running=%d actions=%zu result='%s' summary='%s' "
              "step='%s' details='%s' query_error='%s'\n",
              snapshot.build_running, snapshot.build_actions.size(),
              snapshot.build_result.c_str(), build_failure.summary.c_str(),
              build_failure.next_step.c_str(), build_failure.details.c_str(),
              snapshot.query_error.c_str());
  require(!snapshot.build_running && snapshot.build_error.has_value() &&
              snapshot.build_result.empty(),
          "unconfigured build reports unavailable without fabricated success");
  require(snapshot.query_error.empty(),
          "#99: a build failure is not reported a second time as a query error");
  require(build_failure.summary.find(repository) == std::string::npos &&
              build_failure.details.find(repository) == std::string::npos,
          "#99: no absolute repository path in the build failure");
  auto status = app.status_failure(snapshot);
  require(status && status->panel == PanelId::build &&
              status->summary == build_failure.summary,
          "#99: the status bar names the Build panel, which holds the error");
  require(app.frame_layout().controls.contains("status/failure"),
          "status bar failure is a control");
  click(app, app.frame_layout().controls.at("status/failure"));
  require(app.shell_state_.bottom_panel_visible &&
              app.panels_.active(PanelPlacement::bottom) == PanelId::build &&
              app.frame_layout().controls.contains("build-failure/summary"),
          "#99: the status bar opens the Build panel showing the error");
  capture_themes(app, output, "build-failure");

  key(app, "F9");
  app.editor_graph_->wait_until_idle();
  pulse(app);
  snapshot = app.editor_graph_->snapshot();
  const auto &deploy = snapshot.deploy;
  const auto push_failure =
      describe_push_failure(deploy.failure.value_or(liquid::error{}),
                            deploy.remote, app.repository_->path());
  std::printf("offline: running=%d objects=%llu summary='%s' step='%s' "
              "details='%s' query_error='%s'\n",
              deploy.running, static_cast<unsigned long long>(deploy.objects),
              push_failure.summary.c_str(), push_failure.next_step.c_str(),
              push_failure.details.c_str(), snapshot.query_error.c_str());
  require(!deploy.running && deploy.failure.has_value() && deploy.objects == 0,
          "missing remote reports offline without fabricated upload");
  require(snapshot.query_error.empty(),
          "#99: a push failure appears once, not also as a query error");
  require(push_failure.action == ErrorAction::set_sync_remote &&
              push_failure.details.find(repository) == std::string::npos,
          "#99: no sync remote offers Set lake, with no absolute path");
  require(app.frame_layout().controls.contains("push-failure/summary"),
          "push failure drawn in the Build panel");
  capture_themes(app, output, "push-failure");
}

void NativeAcceptance::wait_for_repository_operation(App &app) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (app.repository_operation_ &&
         std::chrono::steady_clock::now() < deadline) {
    pulse(app);
  }
  pulse(app);
  require(!app.repository_operation_, "repository operation finished");
}

void NativeAcceptance::capture_themes(App &app,
                                      const std::filesystem::path &output,
                                      std::string_view name) {
  for (const auto theme : {ThemePreference::light, ThemePreference::dark}) {
    app.options_.forced_theme = theme;
    pulse(app);
    const auto suffix = theme == ThemePreference::dark ? "-dark.png" : "-light.png";
    save_image(output / (std::string(name) + suffix), capture(app));
  }
  app.options_.forced_theme.reset();
  pulse(app);
}

void NativeAcceptance::clone_failures(App &app,
                                      const std::filesystem::path &output) {
  struct Case {
    std::string_view name;
    int status;          // 0: nothing listens on the port
    std::string_view scheme;
    FailureKind expected;
  };
  const auto cases = std::array<Case, 7>{{
      {"offline", 0, "http", FailureKind::offline},
      {"secure-connection", 404, "https", FailureKind::secure_connection},
      {"unauthorized", 401, "http", FailureKind::unauthorized},
      {"forbidden", 403, "http", FailureKind::forbidden},
      {"not-found", 404, "http", FailureKind::not_found},
      {"lake-unavailable", 503, "http", FailureKind::lake_unavailable},
      {"unsupported-lake", 400, "http", FailureKind::unsupported_lake},
  }};
  for (const auto &item : cases) {
    auto lake = std::optional<CannedLake>{};
    auto port = 0;
    if (item.status != 0) {
      lake.emplace(item.status);
      port = lake->port();
    } else {
      // Bind and release a port, so nothing listens on it during the clone.
      auto probe = std::optional<CannedLake>{};
      probe.emplace(404);
      port = probe->port();
    }
    const auto remote = std::format("{}://127.0.0.1:{}/v1/repositories/sample",
                                    item.scheme, port);
    require(app.frame_layout().controls.contains("front-door/clone"),
            "front door shows Clone from lake");
    const auto button = app.frame_layout().controls.at("front-door/clone");
    std::printf("front-door/clone at %.0f,%.0f %.0fx%.0f phase=%d\n", button.x,
                button.y, button.width, button.height,
                static_cast<int>(app.first_run_.phase()));
    for (int attempt = 0; attempt < 3 && !popup("Clone from lake"); ++attempt) {
      click(app, button);
      pulse(app);
    }
    require(popup("Clone from lake"), "Clone from lake dialog opened");
    // Text entry into ImGui fields is liquify#39; the person's typed values are set here
    // and the dialog's own button starts the clone.
    app.clone_remote_ = remote;
    app.clone_path_ = (output / ("clone-" + std::string(item.name))).string();
    pulse(app);
    click(app, app.frame_layout().controls.at("clone/submit"));
    wait_for_repository_operation(app);
    const auto &failure = app.first_run_.clone_failure();
    std::printf("clone %.*s: kind=%.*s summary='%s' step='%s' details='%s' "
                "lake_requests=%d\n",
                static_cast<int>(item.name.size()), item.name.data(),
                static_cast<int>(failure_kind_name(failure.kind).size()),
                failure_kind_name(failure.kind).data(), failure.summary.c_str(),
                failure.next_step.c_str(), failure.details.c_str(),
                lake ? lake->requests() : 0);
    require(failure.kind == item.expected,
            std::format("{} failure is classified from liquid's error",
                        item.name));
    const auto says_offline =
        failure.next_step.find("offline is normal") != std::string::npos;
    require(says_offline == (item.expected == FailureKind::offline),
            std::format("{}: 'offline is normal' only when unreachable",
                        item.name));
    require(app.first_run_.phase() ==
                (item.expected == FailureKind::offline
                     ? RepositoryLifecyclePhase::offline
                     : RepositoryLifecyclePhase::failed),
            std::format("{}: lifecycle phase matches the failure", item.name));
    require(app.frame_layout().controls.contains("clone-failure/summary") &&
                app.frame_layout().controls.contains("clone-failure/details"),
            std::format("{}: sentence and Details control drawn", item.name));
    require(failure.details.find(output.string()) == std::string::npos,
            std::format("{}: no absolute path in the message", item.name));
    click(app, app.frame_layout().controls.at("clone-failure/details"));
    require(app.error_details_open_["clone-failure"],
            "Details shows liquid's message on request");
    capture_themes(app, output, "clone-" + std::string(item.name));
    click(app, app.frame_layout().controls.at("clone-failure/details"));
    click(app, app.frame_layout().controls.at("clone-failure/back"));
    require(app.first_run_.phase() == RepositoryLifecyclePhase::welcome,
            "Back returns to the front door");
  }
}

int NativeAcceptance::run(std::string_view scenario,
                          const std::filesystem::path &output) {
  std::filesystem::create_directories(output);
  if (scenario.starts_with("front-door-")) {
    App app({});
    const auto launch_status = launch(app, 2);
    if (launch_status != 0) {
      return launch_status;
    }
    require(true, "real native application window initialized");
    pulse(app);
    if (scenario == "front-door-clone-failures") {
      clone_failures(app, output);
    } else {
      throw std::runtime_error("unknown native acceptance scenario");
    }
    std::printf("native acceptance: PASS %.*s\n",
                static_cast<int>(scenario.size()), scenario.data());
    return 0;
  }
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
    build(app, output);
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
