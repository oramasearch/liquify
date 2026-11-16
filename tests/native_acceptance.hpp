// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "liquify/app.hpp"
#include "liquify/screenshot.hpp"

namespace liquify {

// Only the platform adapters are implemented in app.cpp.
class NativeAcceptance {
public:
  static int run(std::string_view scenario,
                 const std::filesystem::path &output);
  static int launch(App &app, int density);
  static void pulse(App &app);
  static void click(App &app, InputRectangle rectangle);
  static void key(App &app, std::string_view name, bool primary = false,
                  bool shift = false);
  static void text(App &app, std::string_view value);
  static ScreenshotImage capture(App &app);
  static bool popup(std::string_view name);
  static void resize(App &app);
  static void fonts(App &app);
  static void source_tab(App &app);
  // macOS: the key equivalent the native menu item for `command` shows ("Cmd+Shift+P", "F6");
  // empty elsewhere.
  static auto menu_key(App &app, std::string_view command) -> std::string;
  // macOS: chooses the native menu item for `command`; false elsewhere.
  static bool choose_menu(App &app, std::string_view command);

private:
  static void open_file(App &app);
  static void geometry(App &app, const std::filesystem::path &output);
  static void edit(App &app, const std::filesystem::path &output);
  static void quick_open(App &app);
  static void palette(App &app);
  static void rebind(App &app);
  static void commands(App &app);
  static void modal_input(App &app);
  static void tabs(App &app);
  static void build(App &app, const std::filesystem::path &output);
  static void cut(App &app, const std::filesystem::path &output);
  static void edit_save_cut(App &app, std::string_view marker,
                            std::string_view message);
  static void push(App &app);
  static auto lake_file(const std::string &remote,
                        const std::filesystem::path &folder, std::string path)
      -> std::string;
  static void clone_push(App &app, const std::filesystem::path &output);
  static void set_lake_push(App &app, const std::filesystem::path &output);
  static void opened_clone_push(const std::filesystem::path &output);
  static void request_review(const std::filesystem::path &output);
  static void set_up_build(const std::filesystem::path &output);
  static void table_headers(App &app, const std::filesystem::path &output);
  static void look(App &app, const std::filesystem::path &output);
  static void front_door(App &app, const std::filesystem::path &output);
  static void fill_clone(App &app, const std::string &remote,
                         const std::string &folder);
  static void wait_for_repository_operation(App &app);
  static void capture_themes(App &app, const std::filesystem::path &output,
                             std::string_view name);
  static void clone_failures(App &app, const std::filesystem::path &output);
};

} // namespace liquify
