# Implementation

`app.cpp` is the single SDL/ImGui translation unit. It owns the logical-to-drawable scale,
checked font upload and retained editor draw callback. The native-window test driver is compiled
here only when `LIQUIFY_NATIVE_WINDOW_TESTS` is enabled, keeping all platform types private.
See the [coordinate contract](../docs/ui/app-shell.md) and [source map](../docs/build/layout.md).
