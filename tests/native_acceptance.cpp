// SPDX-License-Identifier: MIT

#include "native_acceptance.hpp"

#include "../src/editor_buffer.hpp"
#include "../src/repository_view.hpp"
#include "forge_double.hpp"
#include "liquid/drive.hpp"
#include "liquid/forge.hpp"
#include "liquid/identity.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/command.hpp"
#include "liquify/editor_graph.hpp"
#include "liquify/lake_remote.hpp"
#include "ui_property_assert.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

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
  require(popup("Command Palette"), "keyboard opened modal over real source");
  save_image(output / "palette.png", capture(app));
}

void NativeAcceptance::modal_input(App &app) {
  open_file(app);
  click(app, app.frame_layout().editor);
  const auto before = std::string(app.editor_->text());
  key(app, "p", true, true);
  require(popup("Command Palette"), "palette opened over focused source");
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
  require(popup("Quick Open"), "Primary+P opened Quick Open");
  text(app, "other.cpp");
  std::printf("quick-open: query='%s' expected='other.cpp'\n",
              app.quick_open_query_.c_str());
  require(app.quick_open_query_ == "other.cpp",
          "#33: Quick Open receives keyboard query");
  key(app, "Down");
  key(app, "Enter");
  require(app.editor_ && app.editor_->display_path() == "other.cpp" &&
              !popup("Quick Open"),
          "#33: Down/Enter opens selected result and closes Quick Open");
}

void NativeAcceptance::edit(App &app, const std::filesystem::path &output) {
  open_file(app);
  key(app, "p", true);
  require(popup("Quick Open"), "Quick Open opened before focus transition");
  click(app, app.frame_layout().controls.at("quick-open/query"));
  text(app, "main.cpp");
  require(app.quick_open_query_ == "main.cpp",
          "SDL text reached focused ImGui field");
  click(app, app.frame_layout().controls.at("quick-open/main.cpp"));
  require(!popup("Quick Open"), "SDL pointer selected Quick Open result");
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
  require(popup("Command Palette"), "Primary+Shift+P opened palette");
  text(app, "Toggle bottom panel");
  require(app.command_palette_query_ == "Toggle bottom panel",
          "palette receives keyboard query");
  require(app.command_palette_selection_ == 0,
          "#34: the first result is selected");
  key(app, "Down");
  require(app.command_palette_selection_ == 1 ||
              app.commands_->search(app.command_palette_query_).size() == 1,
          "#34: Down moves the selection");
  key(app, "Up");
  require(app.command_palette_selection_ == 0, "#34: Up moves it back");
  key(app, "Enter");
  std::printf("palette: visible=%d bottom_before=%d bottom_after=%d\n",
              popup("Command Palette"), before,
              app.shell_state_.bottom_panel_visible);
  require(!popup("Command Palette") &&
              app.shell_state_.bottom_panel_visible != before,
          "#34: Enter runs the selected palette result");
}

// liquify#34: one source of truth for bindings. A rebind made through the settings surface's
// Keymap field reaches the window's keys, the palette and the native menus at once; the old
// chord cannot bypass it, and a chord two commands claim runs neither and says why.
void NativeAcceptance::rebind(App &app) {
  open_file(app);
  const auto editor = app.frame_layout().editor;
  click(app, editor);
  require(app.editor_focused_, "a click focuses the editor");
  const auto collapse = [&] {
    app.editor_->set_cursor({});
    require(!app.editor_->selection().has_value(), "the selection is collapsed");
  };
  const auto whole_buffer = [&app] {
    const auto selection = app.editor_->selection();
    const auto last = app.editor_->line_count() - 1;
    return selection.has_value() && selection->begin == TextPosition{} &&
           selection->end == TextPosition{last, app.editor_->line(last).size()};
  };

  collapse();
  key(app, "a", true);
  require(whole_buffer(), "#34: default Primary+A selects the buffer through the registry");

  collapse();
  require(app.set_keymap_binding("edit.select-all", "F6"),
          "#34: the Keymap field accepts F6 for Select All");
  require(app.commands_->binding_label("edit.select-all") == "F6",
          "#34: the registry resolves the rebind");
  key(app, "F6");
  require(whole_buffer(), "#34: the rebound key selects the buffer");

  collapse();
  key(app, "a", true);
  std::printf("rebind: old chord selection=%d\n", app.editor_->selection().has_value());
  require(!app.editor_->selection().has_value(),
          "#34: the old hard-coded chord no longer bypasses the rebind");

  collapse();
  key(app, "p", true, true);
  require(popup("Command Palette"), "palette opened");
  text(app, "Select All");
  const auto matches = app.commands_->search(app.command_palette_query_);
  require(!matches.empty() && matches.front().name == "edit.select-all" &&
              app.commands_->binding_label(matches.front().name) == "F6",
          "#34: the palette's first row is Select All, labelled F6");
  key(app, "Enter");
  require(!popup("Command Palette") && whole_buffer(),
          "#34: Enter on the palette row selects the buffer");
  // Back to the editor, away from the first click so it is not read as a double click.
  click(app, {editor.x, editor.y, editor.width / 2.0F, editor.height / 2.0F});
  require(app.editor_focused_, "a click focuses the editor again");

#if defined(__APPLE__)
  const auto menu = menu_key(app, "edit.select-all");
  std::printf("rebind: native menu key equivalent=%s\n", menu.c_str());
  require(menu == "F6", "#34: the native Edit menu shows the rebound key");
  require(menu_key(app, "edit.command-palette") == "Cmd+Shift+P",
          "#34: the native menu shows resolved default bindings");
  collapse();
  require(choose_menu(app, "edit.select-all") && whole_buffer(),
          "#34: choosing the native menu item runs the registry command");
#else
  std::printf("rebind: native menus are macOS only; menu checks skipped\n");
#endif

  collapse();
  require(app.set_keymap_binding("edit.select-all", "Primary+L"),
          "#34: the Keymap field accepts Primary+L");
  require(!app.commands_->conflicts().empty(),
          "#34: Primary+L conflicts with Select Line and is reported");
  key(app, "l", true);
  std::printf("rebind: conflict status='%s'\n", app.editor_status_.c_str());
  require(!app.editor_->selection().has_value() &&
              app.editor_status_.find("is bound to") != std::string::npos,
          "#34: a conflicting chord runs neither command and says why");

  require(app.set_keymap_binding("edit.select-all", "Primary+A"),
          "#34: the Keymap field restores Primary+A");
  require(app.commands_->conflicts().empty(), "#34: the conflict is gone");
  collapse();
  key(app, "a", true);
  require(whole_buffer(), "#34: the restored binding works again");
}

// liquify#34: every built-in command, run through the one dispatcher with a file open, either
// acts or puts the reason it could not in the status bar; none is a silent no-op.
void NativeAcceptance::commands(App &app) {
  open_file(app);
  click(app, app.frame_layout().editor);
  // Not run here: Quit and Close File (a dirty buffer asks in a native dialog) and Reveal (it
  // opens the system file manager). Their handlers are still required below.
  const auto skipped = std::array<std::string_view, 3>{"app.quit", "file.close",
                                                       "repository.reveal"};
  auto names = std::vector<std::string>{};
  for (const auto &command : builtin_commands()) {
    require(App::handles_command(command.name), command.name + " has an implementation");
    if (std::ranges::find(skipped, command.name) == skipped.end()) {
      names.push_back(command.name);
    }
  }
  for (const auto &name : names) {
    const auto *definition = app.commands_->find(name);
    app.editor_status_.clear();
    const auto acted = app.invoke_command(name);
    std::printf("command: %s acted=%d status='%s'\n", name.c_str(), acted,
                app.editor_status_.c_str());
    require(acted || app.editor_status_.starts_with(definition->title + ":"),
            name + " acted or said why not");
    pulse(app);
  }
  const auto before = app.shell_state_.sidebar_visible;
  require(app.invoke_command("view.toggle-sidebar") &&
              app.shell_state_.sidebar_visible != before,
          "#34: view.toggle-sidebar changes the shell state");
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

void NativeAcceptance::cut(App &app, const std::filesystem::path &output) {
  open_file(app);
  click(app, app.frame_layout().editor);
  const auto marker = std::string("/* cut from the native window */");
  text(app, marker);
  require(app.editor_->dirty(), "typed text reached the retained editor");
  key(app, "s", true);
  std::printf("save: dirty=%d status='%s' buffer_error='%s'\n", app.editor_->dirty(),
              app.editor_status_.c_str(), std::string(app.editor_->error()).c_str());
  if (app.editor_->dirty()) {
    auto options = liquid::repository_options{};
    options.session = app.multiplayer_session_;
    auto probe = liquid::runtime::block_on(
        liquid::repository::open(*app.options_.repository_path, options));
    std::printf("probe: path=%s ok=%d error='%s' context='%s'\n",
                app.options_.repository_path->string().c_str(), probe.has_value(),
                probe ? "" : probe.error().message.c_str(),
                probe ? "" : probe.error().context.c_str());
  }
  require(!app.editor_->dirty(), "Primary+S saved the edit as a drip");
  key(app, "k", true, true);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while ((!popup("Cut Change") || !app.cut_pending_) &&
         std::chrono::steady_clock::now() < deadline) {
    pulse(app);
  }
  require(popup("Cut Change"), "Primary+Shift+K opened the Cut Change composer");
  std::printf("composer: drips=%zu files=%zu base='%s'\n",
              app.cut_pending_ ? app.cut_pending_->drips.size() : 0,
              app.cut_pending_ ? app.cut_pending_->paths.size() : 0,
              app.cut_pending_ ? app.cut_pending_->base.c_str() : "");
  // The repository's two seeded writes and the typed save: three drips, no change yet.
  require(app.cut_pending_ && app.cut_pending_->failure.empty() &&
              app.cut_pending_->drips.size() == 3 &&
              app.cut_pending_->paths ==
                  std::vector<std::string>{"main.cpp", "other.cpp"},
          "composer lists the saved drips and the files they touch");
  capture_themes(app, output, "cut-composer-empty");
  click(app, app.frame_layout().controls.at("cut/message"));
  const auto message = std::string("Explain the answer");
  text(app, message);
  require(app.cut_message_ == message, "message typed into the composer");
  require(!app.editor_->dirty() &&
              !std::string(app.editor_->text()).contains(message),
          "#39: the composer's text does not reach the source it covers");
  capture_themes(app, output, "cut-composer");
  click(app, app.frame_layout().controls.at("cut/submit"));
  const auto started = std::chrono::steady_clock::now();
  while ((app.cut_operation_ || popup("Cut Change")) &&
         std::chrono::steady_clock::now() < started + std::chrono::seconds(60)) {
    pulse(app);
  }
  std::printf("cut: waited_ms=%lld operation=%d popup=%d failure='%s' details='%s'\n",
              static_cast<long long>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - started)
                      .count()),
              app.cut_operation_.has_value(), popup("Cut Change"),
              app.cut_failure_.summary.c_str(), app.cut_failure_.details.c_str());
  require(!popup("Cut Change") && app.cut_result_.has_value(),
          "Cut change closed the composer with a new change");
  auto repository = liquid::runtime::block_on(
      liquid::repository::open(app.repository_->path()));
  require(repository.has_value(), "repository reopened independently");
  const auto log = liquid::runtime::block_on(repository->log());
  require(log.has_value() && !log->empty(), "liquid log lists the change");
  const auto &change = log->front();
  auto signer =
      liquid::runtime::block_on(liquid::identity_detail::local_signer());
  require(signer.has_value(), "local identity available");
  std::printf("cut: change=%s message='%s' author=%.*s status='%s'\n",
              liquid::to_string(change.id.value).c_str(),
              change.message.c_str(),
              static_cast<int>(change.author.identifier().size()),
              change.author.identifier().data(), app.editor_status_.c_str());
  require(liquid::to_string(change.id.value) == *app.cut_result_ &&
              change.message == message,
          "the change in liquid's log carries the message");
  require(change.author.identifier() == signer->identity().identifier(),
          "the change is signed by this computer's identity");
  const auto bytes =
      liquid::runtime::block_on(repository->at(change.id));
  require(bytes.has_value(), "the change's snapshot reads back");
  const auto saved = read_back(app.repository_->path());
  require(saved.contains(marker), "the cut change holds the typed edit");
  capture_themes(app, output, "cut-done");
}

// Types into the open editor, saves, and cuts a change through the composer.
void NativeAcceptance::edit_save_cut(App &app, std::string_view marker,
                                     std::string_view message) {
  click(app, app.frame_layout().editor);
  text(app, marker);
  key(app, "s", true);
  std::printf("save: dirty=%d status='%s' buffer_error='%s'\n", app.editor_->dirty(),
              app.editor_status_.c_str(), std::string(app.editor_->error()).c_str());
  if (app.editor_->dirty()) {
    auto options = liquid::repository_options{};
    options.session = app.multiplayer_session_;
    auto probe = liquid::runtime::block_on(
        liquid::repository::open(*app.options_.repository_path, options));
    std::printf("probe: path=%s ok=%d error='%s' context='%s'\n",
                app.options_.repository_path->string().c_str(), probe.has_value(),
                probe ? "" : probe.error().message.c_str(),
                probe ? "" : probe.error().context.c_str());
  }
  require(!app.editor_->dirty(), "Primary+S saved the edit as a drip");
  key(app, "k", true, true);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while ((!popup("Cut Change") || !app.cut_pending_) &&
         std::chrono::steady_clock::now() < deadline) {
    pulse(app);
  }
  require(popup("Cut Change") && app.cut_pending_ &&
              !app.cut_pending_->drips.empty(),
          "Cut Change lists the saved drip");
  click(app, app.frame_layout().controls.at("cut/message"));
  text(app, message);
  click(app, app.frame_layout().controls.at("cut/submit"));
  while ((app.cut_operation_ || popup("Cut Change")) &&
         std::chrono::steady_clock::now() < deadline) {
    pulse(app);
  }
  require(app.cut_result_.has_value(), "the change was cut");
}

// Presses F9 and waits for the push to finish.
void NativeAcceptance::push(App &app) {
  key(app, "F9");
  app.editor_graph_->wait_until_idle();
  pulse(app);
}

// Clones `remote` into a fresh folder with libliquid alone and returns main.cpp.
auto NativeAcceptance::lake_file(const std::string &remote,
                                 const std::filesystem::path &folder,
                                 std::string path) -> std::string {
  {
    auto client = liquid::runtime::block_on(
        liquid::drive_client::open(folder, remote));
    require(client.has_value(), "verification clone opened");
    require(liquid::runtime::block_on(client->pin("")).has_value() &&
                liquid::runtime::block_on(client->sync()).has_value(),
            "verification clone synced from the test lake");
  }
  auto repository =
      liquid::runtime::block_on(liquid::repository::open(folder));
  require(repository.has_value(), "verification clone opened as a repository");
  const auto bytes =
      liquid::runtime::block_on(repository->read(std::move(path)));
  require(bytes.has_value(), "file read from the verification clone");
  return {reinterpret_cast<const char *>(bytes->data()), bytes->size()};
}

void NativeAcceptance::clone_push(App &app, const std::filesystem::path &output) {
  auto lake = liquid::test::forge_double{};
  require(lake.advance_head("main.cpp", source).has_value(),
          "test lake holds main.cpp");
  click(app, app.frame_layout().controls.at("front-door/clone"));
  for (int attempt = 0; attempt < 3 && !popup("Clone from Lake"); ++attempt) {
    click(app, app.frame_layout().controls.at("front-door/clone"));
  }
  require(popup("Clone from Lake"), "Clone from lake dialog opened");
  const auto stamp = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  fill_clone(app, lake.url(), (output / ("clone-" + stamp)).string());
  click(app, app.frame_layout().controls.at("clone/submit"));
  wait_for_repository_operation(app);
  require(app.repository_ != nullptr && app.repository_->error().empty(),
          "the lake repository was cloned and opened");
  const auto recorded = recorded_lake_remote(app.repository_->path());
  std::printf("clone: recorded_remote='%s' lake='%s'\n",
              recorded ? recorded->c_str() : "?", lake.url().c_str());
  require(recorded && *recorded == lake.url(),
          "#104: a clone records the lake it came from");
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline &&
         (!app.frame_layout().controls.contains("repository/main.cpp") ||
          app.repository_->navigation_loading())) {
    pulse(app);
  }
  click(app, app.frame_layout().controls.at("repository/main.cpp"));
  std::printf("clone: opened='%s' bytes=%zu\n",
              app.editor_ ? std::string(app.editor_->display_path()).c_str() : "",
              app.editor_ ? app.editor_->text().size() : 0);
  require(app.editor_ && app.editor_->display_path() == "main.cpp",
          "the cloned main.cpp opened in the editor");
  source_tab(app);
  const auto marker = std::string("/* pushed from the native window */");
  edit_save_cut(app, marker, "Push from liquify");
  push(app);
  const auto &deploy = app.editor_graph_->snapshot().deploy;
  std::printf("push: remote='%s' objects=%llu references=%zu failure='%s'\n",
              deploy.remote.c_str(),
              static_cast<unsigned long long>(deploy.objects),
              deploy.references.size(),
              deploy.failure ? deploy.failure->message.c_str() : "");
  require(!deploy.failure && deploy.remote == lake.url() &&
              !deploy.references.empty() &&
              std::ranges::all_of(deploy.references,
                                  [](const auto &reference) {
                                    return reference.outcome ==
                                           liquid::sync_ref_outcome_kind::applied;
                                  }),
          "#104: F9 pushed the change to the lake it was cloned from");
  capture_themes(app, output, "push-done");
  const auto served = lake_file(lake.url(), output / ("verify-" + stamp), "main.cpp");
  require(served.contains(marker),
          "#104: the lake serves the pushed edit to a fresh clone");
}

void NativeAcceptance::set_lake_push(App &app,
                                     const std::filesystem::path &output) {
  auto lake = liquid::test::forge_double{};
  open_file(app);
  const auto marker = std::string("/* first push */");
  edit_save_cut(app, marker, "First push");
  push(app);
  require(app.editor_graph_->snapshot().deploy.failure.has_value(),
          "F9 with no lake reports it");
  click(app, app.frame_layout().controls.at("status/failure"));
  require(app.frame_layout().controls.contains("push-failure/action"),
          "#104: the push failure offers Set lake");
  capture_themes(app, output, "push-no-lake");
  click(app, app.frame_layout().controls.at("push-failure/action"));
  pulse(app);
  require(popup("Set Lake"), "Set lake opened the Set Lake dialog");
  // The address field has the keyboard when the dialog opens.
  text(app, lake.url());
  require(app.set_lake_url_ == lake.url(), "address typed into Set Lake");
  capture_themes(app, output, "set-lake");
  click(app, app.frame_layout().controls.at("set-lake/submit"));
  std::printf("set-lake: popup=%d failure='%s' details='%s'\n", popup("Set Lake"),
              app.set_lake_failure_.summary.c_str(), app.set_lake_failure_.details.c_str());
  require(!popup("Set Lake") && app.set_lake_failure_.empty(),
          "Set lake recorded the lake");
  push(app);
  const auto &deploy = app.editor_graph_->snapshot().deploy;
  std::printf("push: remote='%s' objects=%llu references=%zu failure='%s'\n",
              deploy.remote.c_str(),
              static_cast<unsigned long long>(deploy.objects),
              deploy.references.size(),
              deploy.failure ? deploy.failure->message.c_str() : "");
  // The test lake already holds a repository of its own, so it refuses this unrelated history;
  // what this proves is that F9 now reaches the lake Set Lake recorded, and says why it refused.
  const auto failure = describe_push_failure(
      deploy.failure.value_or(liquid::error{}), deploy.remote,
      app.repository_->path());
  std::printf("push: summary='%s' step='%s'\n", failure.summary.c_str(),
              failure.next_step.c_str());
  require(deploy.remote == lake.url() && deploy.failure &&
              failure.action != ErrorAction::set_sync_remote &&
              failure.summary.contains("holds a different repository"),
          "#104: F9 pushed to the lake Set Lake recorded");
  capture_themes(app, output, "push-other-history");
}

// A folder cloned from the test lake by libliquid alone, then opened in liquify.
void NativeAcceptance::opened_clone_push(const std::filesystem::path &output) {
  auto lake = liquid::test::forge_double{};
  require(lake.advance_head("main.cpp", source).has_value(),
          "test lake holds main.cpp");
  const auto stamp = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto folder = output / ("clone-" + stamp);
  static_cast<void>(lake_file(lake.url(), folder, "main.cpp"));
  App app({.repository_path = folder});
  require(launch(app, 2) == 0, "real native application window initialized");
  pulse(app);
  open_file(app);
  const auto marker = std::string("/* pushed from the native window */");
  edit_save_cut(app, marker, "Push from liquify");
  push(app);
  const auto &deploy = app.editor_graph_->snapshot().deploy;
  std::printf("push: remote='%s' objects=%llu references=%zu failure='%s'\n",
              deploy.remote.c_str(),
              static_cast<unsigned long long>(deploy.objects),
              deploy.references.size(),
              deploy.failure ? deploy.failure->message.c_str() : "");
  require(!deploy.failure && deploy.remote == lake.url() &&
              !deploy.references.empty() &&
              std::ranges::all_of(deploy.references,
                                  [](const auto &reference) {
                                    return reference.outcome ==
                                           liquid::sync_ref_outcome_kind::applied;
                                  }),
          "#104: F9 pushed the change to the lake the folder was cloned from");
  app.panels_.focus(PanelId::build);
  app.shell_state_.bottom_panel_visible = true;
  pulse(app);
  capture_themes(app, output, "push-done");
  const auto served =
      lake_file(lake.url(), output / ("verify-" + stamp), "main.cpp");
  require(served.contains(marker),
          "#104: the lake serves the pushed edit to a fresh clone");
}

void NativeAcceptance::request_review(const std::filesystem::path &output) {
  // A repository with no lake: the Review panel teaches and offers Set lake.
  {
    const auto local = output / ("local-" + std::to_string(std::chrono::steady_clock::now()
                                                              .time_since_epoch()
                                                              .count()));
    create_repository(local);
    App app({.repository_path = local});
    require(launch(app, 2) == 0, "real native application window initialized");
    app.panels_.focus(PanelId::review);
    app.shell_state_.sidebar_visible = true;
    pulse(app);
    require(app.review_ == nullptr &&
                app.frame_layout().controls.contains("review/set-lake"),
            "#105: with no lake the Review panel offers Set lake, no flag");
    capture_themes(app, output, "review-no-lake");
  }
  auto lake = liquid::test::forge_double{};
  require(lake.advance_head("main.cpp", source).has_value(),
          "test lake holds main.cpp");
  const auto stamp = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto folder = output / ("clone-" + stamp);
  static_cast<void>(lake_file(lake.url(), folder, "main.cpp"));
  const auto before = recorded_lake_remote(folder);
  std::printf("review: recorded_before='%s' error='%s'\n",
              before ? before->c_str() : "", before ? "" : before.error().message.c_str());
  App app({.repository_path = folder});
  require(launch(app, 2) == 0, "real native application window initialized");
  pulse(app);
  std::printf("review: lake_url='%s' status='%s'\n",
              app.options_.lake_url.value_or("").c_str(), app.editor_status_.c_str());
  require(app.review_ != nullptr,
          "#105: the Review panel uses the lake the repository came from");
  app.panels_.focus(PanelId::review);
  app.shell_state_.sidebar_visible = true;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (app.review_->snapshot().busy &&
         std::chrono::steady_clock::now() < deadline) {
    pulse(app);
  }
  pulse(app);
  require(app.frame_layout().controls.contains("review/request-empty"),
          "#105: the empty Review panel offers Request review");
  capture_themes(app, output, "review-empty");
  app.panels_.focus(PanelId::repository);
  pulse(app);
  open_file(app);
  edit_save_cut(app, "/* please review */", "Ask for review");
  push(app);
  require(!app.editor_graph_->snapshot().deploy.failure,
          "the change was pushed");
  app.panels_.focus(PanelId::review);
  app.shell_state_.sidebar_visible = true;
  pulse(app);
  click(app, app.frame_layout().controls.at("review/request"));
  pulse(app);
  require(popup("Request Review"), "Request review opened its dialog");
  text(app, "ana, bo");
  require(app.request_review_reviewers_ == "ana, bo", "reviewers typed");
  capture_themes(app, output, "request-review");
  click(app, app.frame_layout().controls.at("request-review/submit"));
  while ((app.review_->snapshot().busy ||
          !app.review_->snapshot().review.selected) &&
         std::chrono::steady_clock::now() < deadline + std::chrono::seconds(30)) {
    pulse(app);
  }
  const auto snapshot = app.review_->snapshot();
  std::printf("review: selected=%s error='%s'\n",
              snapshot.review.selected ? snapshot.review.selected->id.c_str() : "",
              snapshot.error.c_str());
  require(snapshot.review.selected.has_value(),
          "#105: Request review created the review and opened it");
  auto client = liquid::forge_client{lake.url()};
  const auto reviews = liquid::runtime::block_on(client.list_reviews());
  require(reviews.has_value(), "the test lake lists reviews");
  auto repository = liquid::runtime::block_on(liquid::repository::open(folder));
  require(repository.has_value(), "repository reopened");
  const auto head = repository->status()->head;
  const auto found = std::ranges::find_if(*reviews, [&](const auto &review) {
    return review.id == snapshot.review.selected->id;
  });
  require(found != reviews->end() && head &&
              liquid::to_string(found->change.value) ==
                  liquid::to_string(head->value) &&
              found->reviewers == std::vector<std::string>{"ana", "bo"},
          "#105: the lake holds the same review, of the current change, with "
          "its reviewers");
  capture_themes(app, output, "review-opened");
}

void NativeAcceptance::front_door(App &app, const std::filesystem::path &output) {
  for (const auto *control : {"front-door/open", "front-door/new", "front-door/clone",
                              "front-door/new-window", "front-door/identity"}) {
    require(app.frame_layout().controls.contains(control),
            std::string("front door shows ") + control);
  }
  capture_themes(app, output, "front-door");
  click(app, app.frame_layout().controls.at("front-door/clone"));
  for (int attempt = 0; attempt < 3 && !popup("Clone from Lake"); ++attempt) {
    click(app, app.frame_layout().controls.at("front-door/clone"));
  }
  require(popup("Clone from Lake"), "Clone from Lake dialog opened");
  capture_themes(app, output, "front-door-clone");
  click(app, app.frame_layout().controls.at("clone/cancel"));
  require(!popup("Clone from Lake"), "Cancel closed the clone dialog");

  // Your identity: this computer's signing key, not a lake sign-in.
  click(app, app.frame_layout().controls.at("front-door/identity"));
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!app.first_run_.session() &&
         std::chrono::steady_clock::now() < deadline) {
    pulse(app);
  }
  require(app.first_run_.session().has_value(),
          "Your identity shows this computer's signing identity");
  click(app, app.frame_layout().controls.at("front-door/identity"));
  capture_themes(app, output, "front-door-identity");
  key(app, "Escape");

  // New repository, created with no terminal, then open in the same window.
  click(app, app.frame_layout().controls.at("front-door/new"));
  for (int attempt = 0; attempt < 3 && !popup("New Repository"); ++attempt) {
    click(app, app.frame_layout().controls.at("front-door/new"));
  }
  require(popup("New Repository"), "New repository opened its dialog");
  const auto folder =
      output / ("new-" + std::to_string(std::chrono::steady_clock::now()
                                             .time_since_epoch()
                                             .count()));
  text(app, folder.string());
  require(app.new_repository_path_ == folder.string(), "folder typed");
  capture_themes(app, output, "front-door-new");
  click(app, app.frame_layout().controls.at("new-repository/submit"));
  wait_for_repository_operation(app);
  std::printf("new repository: open=%d error='%s' phase=%d\n",
              app.repository_ != nullptr,
              app.repository_ ? std::string(app.repository_->error()).c_str() : "",
              static_cast<int>(app.first_run_.phase()));
  require(app.repository_ != nullptr && app.repository_->error().empty() &&
              app.repository_->path() == folder,
          "#106: New repository created and opened the repository");
  auto reopened = liquid::runtime::block_on(liquid::repository::open(folder));
  require(reopened.has_value(), "#106: libliquid opens the new repository");
  capture_themes(app, output, "front-door-created");
}

// Types the address into the focused field and the folder into the next, as a person would.
void NativeAcceptance::fill_clone(App &app, const std::string &remote,
                                  const std::string &folder) {
  text(app, remote);
  click(app, app.frame_layout().controls.at("clone/folder"));
  text(app, folder);
  pulse(app);
  require(app.clone_remote_ == remote && app.clone_path_ == folder,
          "address and folder typed into Clone from Lake");
}

// A fresh CMake repository: F7 proposes the CMake command, records it through libliquid's wrap
// API and builds, with no terminal.
void NativeAcceptance::set_up_build(const std::filesystem::path &output) {
  const auto folder =
      output / ("cmake-" + std::to_string(std::chrono::steady_clock::now()
                                               .time_since_epoch()
                                               .count()));
  std::filesystem::create_directories(folder);
  const auto cmake = std::string("cmake_minimum_required(VERSION 3.20)\n"
                                 "project(answer CXX)\n"
                                 "add_executable(answer main.cpp)\n");
  const auto main = std::string("int main() { return 0; }\n");
  std::ofstream(folder / "CMakeLists.txt") << cmake;
  std::ofstream(folder / "main.cpp") << main;
  {
    auto repo = liquid::runtime::block_on(liquid::repository::create(folder));
    require(repo.has_value(), "CMake repository created");
    for (const auto &[name, text] :
         {std::pair{"CMakeLists.txt", cmake}, std::pair{"main.cpp", main}}) {
      const auto bytes = std::as_bytes(std::span(text));
      require(liquid::runtime::block_on(
                  repo->write(name, {bytes.begin(), bytes.end()}))
                  .has_value(),
              std::string("persisted ") + name);
    }
  }
  App app({.repository_path = folder});
  require(launch(app, 2) == 0, "real native application window initialized");
  pulse(app);
  app.editor_graph_->wait_until_idle();
  key(app, "F7");
  app.editor_graph_->wait_until_idle();
  pulse(app);
  require(app.editor_graph_->snapshot().build_error.has_value(),
          "F7 with no recorded build reports it");
  click(app, app.frame_layout().controls.at("status/failure"));
  require(app.frame_layout().controls.contains("build-failure/action"),
          "#107: the build failure offers Set up build");
  click(app, app.frame_layout().controls.at("build-failure/action"));
  pulse(app);
  require(popup("Set Up Build"), "Set up build opened its dialog");
  require(app.build_setup_command_ ==
              "cmake -S . -B build && cmake --build build",
          "#107: the dialog proposes the CMake command and names it");
  capture_themes(app, output, "set-up-build");
  click(app, app.frame_layout().controls.at("build-setup/submit"));
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(120);
  while ((app.build_setup_operation_ || popup("Set Up Build")) &&
         app.build_setup_failure_.empty() &&
         std::chrono::steady_clock::now() < deadline) {
    pulse(app);
  }
  std::printf("set-up-build: failure='%s' details='%s'\n",
              app.build_setup_failure_.summary.c_str(),
              app.build_setup_failure_.details.c_str());
  require(app.build_setup_failure_.empty() && !popup("Set Up Build"),
          "#107: the command was recorded through libliquid's wrap API");
  app.editor_graph_->wait_until_idle();
  pulse(app);
  const auto snapshot = app.editor_graph_->snapshot();
  std::printf("set-up-build: build_error='%s' result='%s' actions=%zu\n",
              snapshot.build_error ? snapshot.build_error->message.c_str() : "",
              snapshot.build_result.c_str(), snapshot.build_actions.size());
  require(!snapshot.build_error ||
              snapshot.build_error->code != liquid::errc::not_configured,
          "#107: F7 now finds the recorded build");
  capture_themes(app, output, "set-up-build-done");
}

// The command palette and the front door's Recent repositories, whose headers liquify#109
// fixed, in the real window and both themes.
void NativeAcceptance::table_headers(App &app, const std::filesystem::path &output) {
  open_file(app);
  key(app, "p", true, true);
  require(popup("Command Palette"), "palette opened");
  require(app.frame_layout().controls.contains("palette/header"),
          "#109: the palette draws its header row");
  capture_themes(app, output, "palette-header");
  key(app, "Escape");
  app.first_run_.return_to_welcome();
  pulse(app);
  require(app.frame_layout().controls.contains("recent-repositories/header"),
          "#109: the front door draws Recent repositories with its header row");
  capture_themes(app, output, "recent-header");
}

// liquify#100-#102: the screens the look issues name, in the real window and both themes:
// the empty editor, a panel header, the command palette, Settings and the front door.
void NativeAcceptance::look(App &app, const std::filesystem::path &output) {
  require(app.frame_layout().controls.contains("editor/empty-title"),
          "#100: the empty editor shows its title");
  require(app.frame_layout().controls.at("editor/empty-title").height >
              app.frame_layout().controls.at("type/body").height * 1.5F,
          "#100: the empty editor title is set in the title size");
  capture_themes(app, output, "look-empty-editor");
  key(app, "p", true, true);
  require(popup("Command Palette"), "palette opened");
  capture_themes(app, output, "look-palette");
  key(app, "Escape");
  static_cast<void>(app.invoke_command("settings.open"));
  pulse(app);
  require(popup("Settings"), "Settings opened");
  capture_themes(app, output, "look-settings");
  key(app, "Escape");
  open_file(app);
  capture_themes(app, output, "look-editor");
  app.panels_.focus(PanelId::search);
  app.shell_state_.sidebar_visible = true;
  pulse(app);
  require(app.frame_layout().controls.contains("search/query"),
          "#101: the Search panel shows its labelled fields");
  capture_themes(app, output, "look-search");
  // #102: the whole window, and a dialog open over it in dark.
  app.shell_state_.bottom_panel_visible = true;
  pulse(app);
  const auto &layout = app.frame_layout().controls;
  require(layout.contains("region/sidebar") && layout.contains("region/editor-area") &&
              std::abs(layout.at("region/sidebar").x + layout.at("region/sidebar").width -
                       layout.at("region/editor-area").x) < 0.5F,
          "#102: the sidebar and editor share an edge");
  capture_themes(app, output, "look-window");
  key(app, "p", true, true);
  app.options_.forced_theme = ThemePreference::dark;
  pulse(app);
  save_image(output / "look-dialog-dark.png", capture(app));
  app.options_.forced_theme.reset();
  key(app, "Escape");
  app.first_run_.return_to_welcome();
  pulse(app);
  require(app.frame_layout().controls.at("welcome/title").height >=
              app.frame_layout().controls.at("welcome/copy").height * 1.5F,
          "#100: the front door title is set in the title size");
  capture_themes(app, output, "look-front-door");
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
    for (int attempt = 0; attempt < 3 && !popup("Clone from Lake"); ++attempt) {
      click(app, button);
      pulse(app);
    }
    require(popup("Clone from Lake"), "Clone from lake dialog opened");
    fill_clone(app, remote,
               (output / ("clone-" + std::string(item.name))).string());
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
    for (int attempt = 0;
         attempt < 3 &&
         app.first_run_.phase() != RepositoryLifecyclePhase::welcome;
         ++attempt) {
      click(app, app.frame_layout().controls.at("clone-failure/back"));
    }
    require(app.first_run_.phase() == RepositoryLifecyclePhase::welcome,
            "Back returns to the front door");
  }
}

int NativeAcceptance::run(std::string_view scenario,
                          const std::filesystem::path &output) {
  std::filesystem::create_directories(output);
  if (scenario == "set-up-build") {
    set_up_build(output);
    std::printf("native acceptance: PASS set-up-build\n");
    return 0;
  }
  if (scenario == "request-review") {
    request_review(output);
    std::printf("native acceptance: PASS request-review\n");
    return 0;
  }
  if (scenario == "opened-clone-push") {
    opened_clone_push(output);
    std::printf("native acceptance: PASS opened-clone-push\n");
    return 0;
  }
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
    } else if (scenario == "front-door-actions") {
      front_door(app, output);
    } else if (scenario == "front-door-clone-push") {
      clone_push(app, output);
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
  } else if (scenario == "rebind") {
    rebind(app);
  } else if (scenario == "commands") {
    commands(app);
  } else if (scenario == "tabs") {
    tabs(app);
  } else if (scenario == "build") {
    build(app, output);
  } else if (scenario == "look") {
    look(app, output);
  } else if (scenario == "table-headers") {
    table_headers(app, output);
  } else if (scenario == "cut") {
    cut(app, output);
  } else if (scenario == "set-lake-push") {
    set_lake_push(app, output);
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
