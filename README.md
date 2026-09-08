# liquify

liquify is the editor built on libliquid's graph: the IDE for liquid, native and cross-platform,
one binary. SDL3 and Dear ImGui are the shell, the buffer is a retained GPU text layer, and
everything liquify knows about repositories, drips, builds, analysis and search comes through
libliquid, pinned as a submodule and linked as a library. It never depends on Zetta or on lake.
liquid is the tool, lake is where it collects, liquify is where you work. The longer statement,
and why it is a native editor rather than an extension, is in
[`docs/design/overview.md`](docs/design/overview.md).

## Status

`liquify` opens a repository front door with local open, lake clone, recent repositories, identity
session and additional-window actions. `liquify <path>` opens a liquid repository, edits blobs in
a retained GPU text buffer and saves
through libliquid as a drip; C and C++ buffers add graph-backed diagnostics, navigation and
completion, structural editing, and F7, F8 and F9 build, test and deploy through the attached
daemon. The editor adds multi-cursor and column editing, repository-configured indentation,
liquid syntax and collaboration gutters, search/replace, and operation-log undo/redo.
Quick Open, Symbol Search, definitions, references, call hierarchy, breadcrumbs, recent
locations, history traversal, and repository context actions share registered default-key commands.
`--lake` adds review in place with cursor-anchored comments. Multiplayer surfaces presence,
per-line attribution and semantic lease controls, including lease events delivered to native
plugins. Native plugins register queries, commands and panels, and the assistant is off by default
and opens no connection while off. `design/` carries the stamped Orama DTCG snapshot, generated
native roles and embedded IBM Plex assets. The light/dark application shell hosts every surface
through one panel registry; dock, editor-tab and floating placement plus focus persist per
repository, layouts reset to their defaults, and one channel reports long-operation progress.
Native macOS menus remain available. Searchable `.liquid` settings combine user preferences with
repository overrides and apply theme, fonts, editor behavior, keymaps, optional AI and lake
accounts live; telemetry remains off by default and credentials are masked. The shared command
registry supplies the fuzzy command palette, platform bindings, conflict reporting, macOS menus
and headless `--command` invocation; its generated reference is in
[`docs/ui/commands.md`](docs/ui/commands.md). Pull-request CI
packages macOS, Windows and Linux installers
and `v*` tags publish them; [`docs/design/status.md`](docs/design/status.md) lists each area with
its page, and the specification is `oramasearch/liquid` `docs/features/21-liquify-ide.md`.
SDL text composition supports CJK input methods and dead keys; the editor exchanges rich and plain
clipboard forms and accepts desktop file drops. Keyboard navigation has visible focus, macOS
exposes labelled accessibility elements, and high contrast plus the OS reduced-motion preference
apply across the shell.

## Build and run

Requirements on macOS: CMake 3.28 or newer, Ninja, Homebrew LLVM (Clang 17 or newer), Node.js,
OpenSSL and zlib (`brew install llvm cmake ninja node openssl@3`). liquid, the assistant transport
and tree-sitter are submodules; SDL3, Dear ImGui and GoogleTest are fetched by CMake, and SDL is
linked static so the result is one binary.

```sh
git clone --recurse-submodules git@github.com:oramasearch/liquify.git
cd liquify
git submodule update --init --recursive
cmake -S . -B build -G Ninja
cmake --build build -j 6
ctest --test-dir build --output-on-failure
./build/liquify version
./build/liquify --smoke 30
```

`version` and the unit tests run without a display; the native rendering test requires a Retina
desktop on macOS (see [`docs/ui/app-shell.md`](docs/ui/app-shell.md)); `--smoke` opens a window, scrolls and edits a
50,000-line buffer for 30 frames, prints latency metrics and exits 0. `./build/liquify <path>`
opens a liquid repository; `--print-tree`, `--apply-edits` and `--lake` are listed with the rest
of the build in [`docs/build/local.md`](docs/build/local.md). Linux requirements and the CI
toolchains are in [`docs/build/ci.md`](docs/build/ci.md).

Until versioned releases identify compatible builds, use these artifact combinations:

| Artifact | Supported revision |
|---|---|
| Separately built liquid CLI and liquify's pinned libliquid | `7ff6ed82f93b916ef206c5776e2761b19536d8f3` |
| Native lake service used for clone verification | `620c249c3e03c455b1392d4691488e7639d01ff6` |

`liquify version` identifies the IDE and library revisions. The store compatibility rule,
upgrade diagnostics, and independent CLI → IDE → CLI integration check are in
[`docs/build/store-compatibility.md`](docs/build/store-compatibility.md).

## Documentation
| Path | Role |
|---|---|
| `include/liquify/app.hpp`, `src/app.cpp` | the application shell: window, renderer, ImGui context, frame loop. **The one TU that includes SDL3 or ImGui.** |
| `include/liquify/input.hpp`, `src/input.cpp` | the plain-C++ input and accessibility state machine: composition, clipboard, drops, preferences, and platform labels |
| `src/editor_buffer.hpp`, `src/editor_buffer.cpp` | the in-memory text buffer, input modes, and libliquid blob read/write path |
| `src/text_layout.hpp`, `src/text_layout.cpp` | retained per-blob line layout with edit-local invalidation and UTF-8/tab positioning |
| `src/repository_view.hpp`, `src/repository_view.cpp` | the plain C++ repository loader: reads the root on a worker, loads nested trees on expansion, and exposes entries, open latency, or an error |
| `include/liquify/version.hpp`, `src/version.cpp` | version |
| `include/liquify/stub_tree.hpp`, `src/stub_tree.cpp` | tree-sitter C/C++ declaration stubs, their liquid graph query, and cross-file stub resolver |
| `include/liquify/interactive_analysis.hpp`, `src/interactive_analysis.cpp` | open-buffer incremental trees and registered cursor, completion, hover, reference and diagnostics queries |
| `include/liquify/editor_graph.hpp`, `src/editor_graph.cpp` | background editor-query runtime, daemon host/attach lifetime, asynchronous query snapshots and status data |
| `docs/editor/build.md` | graph build, test and deploy panels, event states, bindings and measurement record |
| `include/liquify/structural_edit.hpp`, `src/structural_edit.cpp` | canonical node-level edit records, selection, rename, extract-function and repository application |
| `include/liquify/review.hpp`, `src/review.cpp` | coroutine-native review model/controller: forge listing and state, sync pull, text diff, cursor comments, and anchor advancement |
| `include/liquify/context_builder.hpp`, `src/context_builder.cpp` | byte-budgeted, inspectable assistant context assembled from graph facts |
| `include/liquify/ai_provider.hpp`, `src/ai_provider.cpp` | streaming provider interface and Anthropic Messages implementation |
| `include/liquify/ai_config.hpp`, `src/ai_config.cpp` | default-off repository/global configuration and credential lookup |
| `include/liquify/assistant.hpp`, `src/assistant.cpp` | assistant state, proposal acceptance, and local assisted-edit record |
| `include/liquify/plugin.hpp`, `src/plugin.cpp` | checked native plugin host, global/repository configuration, canonical-CBOR validation, graph query registration, commands, and panel view models |
| `include/liquify/multiplayer.hpp`, `src/multiplayer.cpp` | liquid-backed presence, per-line attribution, lease operations, session identity, and editor display text |
| `plugins/sample/` | sample query, command, and panel built for static and dynamic registration |
| `docs/multiplayer.md` | multiplayer editor surface, plugin callback delivery, authorization boundary, and verification |
| `packaging/` | hand-built macOS, Windows, AppImage, and Flatpak release inputs used until liquid's projection targets land |
| `src/main.cpp` | CLI: optional repository path, `--lake URL`, `--print-tree`, `--apply-edits`, `version`, `--smoke [N]`, `--help` |
| `tests/` | GoogleTest; `ctest --test-dir build` |
| `modules/liquid` | **libliquid**, pinned as a submodule; built library-only (`LIQUID_BUILD_CLI OFF`), linked as `liquid::liquid` |
| `modules/corosio`, `modules/http` | pinned TLS coroutine and HTTP/1.1 transport used only by the assistant provider |
| `modules/tree-sitter*` | pinned tree-sitter runtime plus generated C and C++ grammars |

[`docs/README.md`](docs/README.md) indexes one folder per topic. Every change to the system lands
its page there in the same change; this file is only the front door. The most useful pages:

- [`docs/design/overview.md`](docs/design/overview.md): what liquify is and what it is built on.
- [`docs/design/status.md`](docs/design/status.md): what the source tree does, by area.
- [`docs/app-shell/README.md`](docs/app-shell/README.md): the SDL3 and ImGui shell, the CLI, and
  how the liquid pin is bumped.
- [`docs/ui/design-system.md`](docs/ui/design-system.md): the stamped Orama tokens, generated
  native constants, IBM Plex assets, and token gate.
- [`docs/ui/app-shell.md`](docs/ui/app-shell.md): the persisted editor layout, themes, native
  macOS conventions, and light/dark screenshot matrix.
- [`docs/ui/accessibility.md`](docs/ui/accessibility.md): text composition, rich/plain clipboard,
  file drops, keyboard focus order, platform labels, high contrast, and reduced motion.
- [`docs/ui/first-run.md`](docs/ui/first-run.md): first run, repository switching, lake clone,
  identity session, recovery states, multiple windows, and status signals.
- [`docs/ui/panels.md`](docs/ui/panels.md): the panel registry, docking, layout persistence,
  commands, states, and shared progress channel.
- [`docs/ui/editor.md`](docs/ui/editor.md): the editor interaction model, syntax, gutters, search,
  history and editor-view screenshots.
- [`docs/ui/settings.md`](docs/ui/settings.md): searchable `.liquid` settings, repository overrides,
  live application, validation, credentials and telemetry.
- [`docs/editor/README.md`](docs/editor/README.md): the buffer, the retained text layer and the
  smoke measurements.
- [`docs/editor/interactive-analysis.md`](docs/editor/interactive-analysis.md),
  [`docs/editor/structural-editing.md`](docs/editor/structural-editing.md),
  [`docs/editor/review.md`](docs/editor/review.md), [`docs/editor/ai.md`](docs/editor/ai.md),
  [`docs/editor/build.md`](docs/editor/build.md): analysis, structural edits, review in place,
  the assistant, and build, test and deploy.
- [`docs/plugins/README.md`](docs/plugins/README.md): the native plugin ABI.
- [`docs/ui/README.md`](docs/ui/README.md): deterministic light and dark screenshot proof.
- [`docs/build/layout.md`](docs/build/layout.md): the source tree.
- [`docs/packaging/README.md`](docs/packaging/README.md): installers, signing and verification.

liquify is part of the liquid project;
[`oramasearch/liquid-bridge`](https://github.com/oramasearch/liquid-bridge) holds the
cross-module map and the action plan.

## Repository rules

[`CLAUDE.md`](CLAUDE.md) is the instruction file for every contributor and agent: the recorded
decisions, the one-translation-unit rule for SDL and ImGui, the liquid pin, what never changes,
and the full local proof to run before finishing a change. [`AGENTS.md`](AGENTS.md) restates the
non-negotiables for Codex.

## Licence

No licence file is declared. The repository is private for now and open source eventually, like
liquid; the licence decision is tracked in
[oramasearch/liquid-bridge#2](https://github.com/oramasearch/liquid-bridge/issues/2).
