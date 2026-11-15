#pragma once

#include <filesystem>
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

private:
  static void open_file(App &app);
  static void geometry(App &app, const std::filesystem::path &output);
  static void edit(App &app, const std::filesystem::path &output);
  static void quick_open(App &app);
  static void palette(App &app);
  static void modal_input(App &app);
  static void tabs(App &app);
  static void build(App &app);
  static void wait_for_repository_operation(App &app);
  static void capture_themes(App &app, const std::filesystem::path &output,
                             std::string_view name);
  static void clone_failures(App &app, const std::filesystem::path &output);
};

} // namespace liquify
