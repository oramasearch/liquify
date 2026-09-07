# liquify

liquify is the editor built on libliquid's graph: the IDE for liquid, native and cross-platform,
one binary. SDL3 and Dear ImGui are the shell, the buffer is a retained GPU text layer, and
everything liquify knows about repositories, drips, builds, analysis and search comes through
libliquid, pinned as a submodule and linked as a library. It never depends on Zetta or on lake.
liquid is the tool, lake is where it collects, liquify is where you work. The longer statement,
and why it is a native editor rather than an extension, is in
[`docs/design/overview.md`](docs/design/overview.md).

## Status

`liquify <path>` opens a liquid repository, edits blobs in a retained GPU text buffer and saves
through libliquid as a drip; C and C++ buffers add graph-backed diagnostics, navigation and
completion, structural editing, and F7, F8 and F9 build, test and deploy through the attached
daemon. The editor adds multi-cursor and column editing, repository-configured indentation,
liquid syntax and collaboration gutters, search/replace, and operation-log undo/redo.
`--lake` adds review in place with cursor-anchored comments, native plugins register
queries, commands and panels, and the assistant is off by default and opens no connection while
off. A shared registry makes every shell, editor, panel, and plugin command fuzzy-searchable,
rebindable through typed `settings.liquid` keymaps, visible in native macOS menus, and addressable
by `--command`; its generated reference is [`docs/ui/commands.md`](docs/ui/commands.md).
`design/` carries the stamped Orama DTCG snapshot, generated native roles and embedded IBM
Plex assets. The light/dark application shell hosts those panels in a persisted repository-local
layout with native macOS menus. The shell hosts every surface through one panel registry; dock,
editor-tab and floating placement plus focus persist per repository, layouts reset to their
defaults, and one channel reports long-operation progress. Pull-request CI packages macOS,
Windows and Linux installers and `v*` tags publish them;
[`docs/design/status.md`](docs/design/status.md) lists each area with its page, and the
specification is `oramasearch/liquid` `docs/features/21-liquify-ide.md`.
> **Status: retained GPU editor with graph-backed analysis, multiplayer presence and attribution, lease controls, inline build, test and deploy, structural editing, review in place, native plugins, and an optional AI surface.**
> `liquify <path>` opens a liquid repository through the public coroutine API, renders its root
> entries, loads nested trees on expansion, and opens blobs into an editable in-memory buffer;
> save writes through libliquid as a drip. Unreadable repositories show liquid's error and make
> `--print-tree` exit non-zero;
> `liquify` with no path retains the hello state, and `--smoke` scrolls and edits 50,000 lines.
> The pin includes liquid's compact-checkpoint repository format, Phase 2 and 3 APIs, and the
> persisted demand-driven query graph, tree-diff and record-enumeration APIs, per-checkout daemon,
> registered frontend modules with per-definition C/C++ actions, C/C++ stub-tree query,
> text-anchor mapping, and the coroutine-native forge review client used by review-in-place
> workflows. Stub trees resolve declarations from closed files without retaining their full parse
> trees. Open
> overlays retain error-tolerant incremental trees and answer node,
> enclosing-definition, definition, reference, hover, completion and build-diagnostic queries;
> an edit cancels their in-flight graph work. The editor renders graph diagnostics inline and in a
> panel, exposes definition, hover and completion controls, and shows daemon drip triggers plus the
> validation backlog in its status line. Syntax-node selection, cross-file rename and C/C++
> extract-function run on the editor worker and preview fingerprinted node edits before applying
> one drip per changed file. The same canonical CBOR records are accepted by the headless
> `--apply-edits` command. With `--lake`, the review panel pulls lake changes, renders retained
> inline text hunks and conflict values, posts cursor-anchored comments, advances their exact,
> moved, approximate, or deleted positions across revised heads, and updates review state. The
> default-off assistant builds inspectable, byte-budgeted
> context from those graph facts, streams the Anthropic Messages API over pinned Corosio and
> Boost.HTTP, and applies a proposed edit only after user acceptance. Turning it off constructs no
> provider and opens no connection.
> Native plugins share liquid's versioned frontend C boundary, register graph queries, commands,
> and canonical-CBOR panels, and load through checked static or configured dynamic paths. Plugin
> commands appear in the palette and data-only panels render below analysis results. The sample
> proves all three registrations; load and panel failures remain local to their source.
> Plugins may also subscribe to liquid's ABI-v1 presence, lease, and attribution events. The editor
> displays active sessions, nearby line attribution including delegated-agent writes, and lease
> holders; it obtains and releases file leases through liquid and refuses covered edits before a
> local buffer mutation.
> F7 plans and starts the configured build through the attached daemon, then renders its action
> event stream, critical path and scheduler decisions. F8 lists and runs graph test actions; F9
> pushes saved drips to the configured sync remote and reports every reference outcome, including
> an authorization refusal while accepted references continue. Failed action diagnostics render on
> their affected editor lines.
> Pull-request CI also produces a hardened-runtime macOS `pkg` and `dmg`, a signed and install-tested
> Windows MSIX, and an AppImage exercised on Ubuntu and Fedora. `v*` tags publish those installers
> with signed and unsigned SHA-256 records; notarization runs when the release secrets are present.
> The specification lives in
> [`oramasearch/liquid`](https://github.com/oramasearch/liquid) under
> [`docs/features/21-liquify-ide.md`](https://github.com/oramasearch/liquid/blob/main/docs/features/21-liquify-ide.md).

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

`version` and the tests run without a display; `--smoke` opens a window, scrolls and edits a
50,000-line buffer for 30 frames, prints latency metrics and exits 0. `./build/liquify <path>`
opens a liquid repository; `--print-tree`, `--apply-edits` and `--lake` are listed with the rest
of the build in [`docs/build/local.md`](docs/build/local.md). Linux requirements and the CI
toolchains are in [`docs/build/ci.md`](docs/build/ci.md).

## Documentation
| Path | Role |
|---|---|
| `include/liquify/app.hpp`, `src/app.cpp` | the application shell: window, renderer, ImGui context, frame loop. **The one TU that includes SDL3 or ImGui.** |
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
- [`docs/ui/commands.md`](docs/ui/commands.md): the generated command registry, platform defaults,
  typed keymap overrides, conflicts, native menus, and headless invocation.
- [`docs/ui/panels.md`](docs/ui/panels.md): the panel registry, docking, layout persistence,
  commands, states, and shared progress channel.
- [`docs/ui/editor.md`](docs/ui/editor.md): the editor interaction model, syntax, gutters, search,
  history and editor-view screenshots.
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
