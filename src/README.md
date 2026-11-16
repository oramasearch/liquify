# Implementation

`app.cpp` is the single SDL/ImGui translation unit. It owns the logical-to-drawable scale,
checked font upload and retained editor draw callback. The native-window test driver is compiled
here only when `LIQUIFY_NATIVE_WINDOW_TESTS` is enabled, keeping all platform types private.
See the [coordinate contract](../docs/ui/app-shell.md) and [source map](../docs/build/layout.md).

`app.cpp` owns the native rendering and input loop, including the default-window hierarchy,
toolbar groups, panel details, identifier abbreviation, and line-gutter collaboration affordances.
Repository and editor workers use the pinned public libliquid API. `repository_error.cpp` adds
artifact and upgrade guidance when that API rejects an unsupported store format, preserving
unrelated errors. `version.cpp` reports the configured IDE and library revisions. See
[what the window shows](../docs/ui/what-the-window-shows.md) and
[store compatibility](../docs/build/store-compatibility.md) for the UI and proof contracts.

`plugin_host_main.cpp` is the `liquify-plugin-host` executable: it applies the platform sandbox,
maps one plugin library and answers requests. `plugin_process.cpp` starts and supervises it from
the IDE (deadlines, restarts, the Windows token and job), and `plugin_protocol.cpp` is the framed
message format both link. `plugin.cpp` routes a loaded plugin's queries, commands, panels and
events through that process; it no longer maps plugin libraries. See
[process isolation](../docs/plugins/README.md#process-isolation).

`app.cpp` replaces its plain C++ frame layout report each frame, including submitted text bounds
and all panel placements. Native rendering properties are described in
[layout properties](../docs/ui/property-tests.md).

The native acceptance adapters enqueue SDL pointer, key and text events. Repository rows and
Quick Open controls contribute current rectangles to the frame report. See
[native acceptance](../docs/ui/native-acceptance.md).
