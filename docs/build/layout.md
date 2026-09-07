# Source tree layout

One binary. `src/app.cpp` is the one translation unit that includes SDL3 or ImGui, and
`src/ai_provider.cpp` is the only network implementation; every other file is plain C++ over
libliquid's public API.

| Path | Role |
|---|---|
| `include/liquify/app.hpp`, `src/app.cpp` | the token-derived application shell: window, native macOS menus, renderer, ImGui context, persisted panel layout, frame loop, and screenshot renderer. **The one TU that includes SDL3 or ImGui.** |
| `include/liquify/app_state.hpp`, `src/app_state.cpp` | bounded repository-local shell layout, active surfaces, and theme persistence |
| `include/liquify/input.hpp`, `src/input.cpp` | plain-C++ composition, drop routing, clipboard conversion, motion preference and accessibility metadata |
| `include/liquify/command.hpp`, `src/command.cpp` | command registry, platform key chords, fuzzy search, recency, typed keymap overrides, conflicts, and generated reference/default-keymap text |
| `include/liquify/panel.hpp`, `src/panel.cpp` | plain-C++ panel descriptors, commands, placement, focus, runtime state and shared operation notifications |
| `include/liquify/settings.hpp`, `src/settings.cpp` | typed `.liquid` user settings, repository overlays, validation, serialization, search and group reset |
| `include/liquify/screenshot.hpp`, `src/screenshot.cpp` | named screenshot registry plus the in-tree RGBA PNG reader and writer |
| `include/liquify/editor_model.hpp`, `src/editor_model.cpp` | pure selection, repository preferences, find/replace, syntax-span, bracket, fold and visual-row functions |
| `src/editor_buffer.hpp`, `src/editor_buffer.cpp` | the in-memory multi-cursor text buffer, input modes, and libliquid blob read/write path |
| `src/text_layout.hpp`, `src/text_layout.cpp` | retained per-blob line layout with edit-local invalidation and UTF-8/tab positioning |
| `design/`, `include/liquify/design_tokens.generated.hpp` | stamped Orama DTCG and IBM Plex inputs, reviewed role map, dependency-free compiler, generated native palettes and type scale, and tamper/staleness gate |
| `src/repository_view.hpp`, `src/repository_view.cpp` | the plain C++ repository loader: reads the root on a worker, loads nested trees on expansion, and exposes entries, open latency, or an error |
| `include/liquify/version.hpp`, `src/version.cpp` | version |
| `include/liquify/stub_tree.hpp`, `src/stub_tree.cpp` | tree-sitter C/C++ declaration stubs, their liquid graph query, and cross-file stub resolver |
| `include/liquify/interactive_analysis.hpp`, `src/interactive_analysis.cpp` | open-buffer incremental trees and registered cursor, completion, hover, reference and diagnostics queries |
| `include/liquify/editor_graph.hpp`, `src/editor_graph.cpp` | background editor-query runtime, daemon host/attach lifetime, syntax/collaboration/search/history snapshots and status data; the editor view is in [`../ui/editor.md`](../ui/editor.md) and the build, test and deploy panels are in [`../editor/build.md`](../editor/build.md) |
| `include/liquify/structural_edit.hpp`, `src/structural_edit.cpp` | canonical node-level edit records, selection, rename, extract-function and repository application |
| `include/liquify/review.hpp`, `src/review.cpp` | coroutine-native review model/controller: forge listing and state, sync pull, text diff, cursor comments, and anchor advancement |
| `include/liquify/context_builder.hpp`, `src/context_builder.cpp` | byte-budgeted, inspectable assistant context assembled from graph facts |
| `include/liquify/ai_provider.hpp`, `src/ai_provider.cpp` | streaming provider interface and Anthropic Messages implementation |
| `include/liquify/ai_config.hpp`, `src/ai_config.cpp` | default-off repository/global configuration and credential lookup |
| `include/liquify/assistant.hpp`, `src/assistant.cpp` | assistant state, proposal acceptance, and local assisted-edit record |
| `include/liquify/plugin.hpp`, `src/plugin.cpp` | checked native plugin host, global/repository configuration, canonical-CBOR validation, graph query registration, commands, and panel view models |
| `plugins/sample/` | sample query, command, and panel built for static and dynamic registration |
| `packaging/` | hand-built macOS, Windows, AppImage, and Flatpak release inputs used until liquid's projection targets land |
| `bench/` | stub-tree benchmarks, built with `LIQUIFY_BUILD_BENCH` |
| `src/main.cpp` | CLI: optional repository path, `--lake URL`, `--print-tree`, `--apply-edits`, `--command NAME`, `docs commands|keymap`, `--screenshot STATE --out PNG`, `version`, `--smoke [N]`, `--help` |
| `tests/` | GoogleTest, including screenshot pixel-region assertions; `ctest --test-dir build` |
| `tools/ui/` | refreshes every named screenshot under `docs/ui/screenshots/` |
| `modules/liquid` | **libliquid**, pinned as a submodule; built library-only (`LIQUID_BUILD_CLI OFF`), linked as `liquid::liquid` |
| `modules/corosio`, `modules/http` | pinned TLS coroutine and HTTP/1.1 transport used only by the assistant provider |
| `modules/tree-sitter*` | pinned tree-sitter runtime plus generated C and C++ grammars |
| `docs/` | one folder per topic ([`../README.md`](../README.md)) |

A new area adds its header under `include/liquify/`, its source under `src/`, its tests under
`tests/`, and a row here.
