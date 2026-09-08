#pragma once

#include <string_view>

namespace liquify {
class App;

// Display-required issue #55 regression. Platform events live in app.cpp.
class NativeBufferRegression {
public:
    static int run(App& app);
    static void text(std::string_view value);
    static void key(char value, bool shift = false);
    static void close_window(App& app);
    static void answer_dialog(std::string_view label);
    static void describe_window(App& app);

private:
    static void pulse(App& app);
    static void switches(App& app);
    static void close_file(App& app);
    static void save_failure(App& app);
    static void switch_repository(App& app);
    static void quit(App& app);
};
} // namespace liquify
