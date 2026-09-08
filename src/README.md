# Implementation

`app.cpp` is the single SDL/ImGui translation unit. It owns the logical-to-drawable scale,
checked font upload and retained editor draw callback. The native-window test driver is compiled
here only when `LIQUIFY_NATIVE_WINDOW_TESTS` is enabled, keeping all platform types private.
See the [coordinate contract](../docs/ui/app-shell.md) and [source map](../docs/build/layout.md).

`app.cpp` owns the native rendering and input loop. Repository and editor workers use the pinned
public libliquid API. `repository_error.cpp` adds artifact and upgrade guidance when that API
rejects an unsupported store format, preserving unrelated errors. `version.cpp` reports the
configured IDE and library revisions. See
[store compatibility](../docs/build/store-compatibility.md) for the format and proof contract.
