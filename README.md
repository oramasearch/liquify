# liquify

> **Status: experimental.** liquify is under active development and not ready for daily work.
> Expect missing features, rough edges, changes to its settings and keymaps without notice, and
> bugs that can lose unsaved work. There are no versioned releases yet. Use it to try liquid's
> editing model, not on code you cannot afford to redo.

liquify is the editor built on libliquid's graph: the IDE for liquid, native and cross-platform,
one binary. SDL3 and Dear ImGui are the shell, the buffer is a retained GPU text layer, and
everything liquify knows about repositories, drips, builds, analysis and search comes through
libliquid, built from a liquid checkout beside this one and linked as a library. It never depends on Zetta or on lake.
liquid is the tool, lake is where it collects, liquify is where you work. The longer statement,
and why it is a native editor rather than an extension, is in
[`docs/design/overview.md`](docs/design/overview.md).

## Status

What works today, in a native window on macOS (Linux and Windows build from the same source):

- **Front door.** Open a folder, create a repository, clone one from lake, or reopen a recent one;
  your local signing identity; more windows. Repositories switch without a restart.
- **Editor.** Retained GPU text buffers with multi-cursor and column editing, search and replace,
  folds, liquid syntax, and per-buffer undo. Save writes a drip through libliquid; Cut Change
  (Primary+Shift+K) names the saved drips as a change.
- **Navigation and analysis.** Quick Open, symbol search, definitions, references, breadcrumbs and
  history. C and C++ buffers get graph-backed diagnostics, completion and structural edits
  (rename, extract function).
- **Lake.** F9 pushes to the lake a repository was cloned from (or one you set). The Review panel
  lists, opens and requests reviews on that lake, with cursor-anchored comments and approval.
- **Build.** F7 builds and F8 runs tests through liquid's daemon; with no recorded build, F7
  proposes a command and records it.
- **Shell.** One panel system (dock, float, tabs), a command palette with one row per action and
  rebindable keys, searchable `.liquid` settings, light and dark themes on the Orama design tokens
  and IBM Plex, accessibility labels, high contrast and reduced motion.
- **Optional.** Native plugins (repository-named ones load only once you trust their digest),
  multiplayer presence and attribution, and an AI assistant that is off by default and opens no
  connection while off.

Known gaps are tracked as issues; [`docs/design/status.md`](docs/design/status.md) lists every area
with its page, and the specification is `oramasearch/liquid` `docs/features/21-liquify-ide.md`.

## Build and run

Requirements on macOS: CMake 3.28 or newer, Ninja, Homebrew LLVM (Clang 17 or newer), Node.js,
OpenSSL and zlib (`brew install llvm cmake ninja node openssl@3`). liquid is **not** a submodule:
clone it next to liquify, where the build looks for it (`../liquid`), or pass its path with
`-DLIQUIFY_LIQUID_SOURCE_DIR`. The assistant transport and tree-sitter are submodules; SDL3, Dear
ImGui and GoogleTest are fetched by CMake, and SDL is linked static so the result is one binary.

```sh
git clone --recurse-submodules https://github.com/oramasearch/liquid.git
git clone --recurse-submodules https://github.com/oramasearch/liquify.git
cd liquify
cmake -S . -B build -G Ninja
cmake --build build -j 6
ctest --test-dir build --output-on-failure
./build/liquify version
./build/liquify --smoke 30
```

`version` and the unit tests run without a display; native rendering and workflow acceptance tests
require a logged-in desktop on macOS (see [native acceptance](docs/ui/native-acceptance.md),
[`docs/ui/app-shell.md`](docs/ui/app-shell.md) and
[layout property proof](docs/ui/property-tests.md)); `--smoke` opens a window, scrolls and edits a
50,000-line buffer for 30 frames, prints latency metrics and exits 0. `./build/liquify <path>`
opens a liquid repository; `--print-tree`, `--apply-edits` and `--lake` are listed with the rest
of the build in [`docs/build/local.md`](docs/build/local.md). There is no hosted CI: GitHub Actions
are off, and tests run when a release is cut. Linux requirements are in [`docs/build/ci.md`](docs/build/ci.md).

There are no versioned releases yet, so liquid and liquify are not yet paired by version: build
from both repositories' default branches. `liquify version` identifies the IDE and library
revisions. The store compatibility rule,
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
| `include/liquify/user_error.hpp`, `src/user_error.cpp` | the error model: each failure as a sentence, a next step and details, classified from liquid's error codes |
| `include/liquify/change_cut.hpp`, `src/change_cut.cpp` | the pending change and Cut Change through libliquid's cut API |
| `include/liquify/lake_remote.hpp`, `src/lake_remote.cpp` | the lake a repository was cloned from, used by push and review |
| `include/liquify/build_setup.hpp`, `src/build_setup.cpp` | the proposed build command and recording it through libliquid's wrap API |
| `docs/editor/build.md` | graph build, test and push panels, build setup, event states, bindings and measurement record |
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
| `packaging/` | macOS, Windows, AppImage, and Flatpak release inputs, built locally when a release is cut |
| `src/main.cpp` | CLI: optional repository path, `--lake URL` (for scripts; the window uses the repository's lake), `--print-tree`, `--apply-edits`, `plugin trust`/`plugin list`, `version`, `--smoke [N]`, `--help` |
| `tests/` | GoogleTest; `ctest --test-dir build` |
| `../liquid` (`LIQUIFY_LIQUID_SOURCE_DIR`) | **libliquid** — a *sibling* checkout, not a submodule here; built library-only (`LIQUID_BUILD_CLI OFF`), linked as `liquid::liquid` |
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
  the assistant, and build, test and push.
- [`docs/plugins/README.md`](docs/plugins/README.md): the native plugin ABI.
- [`docs/ui/README.md`](docs/ui/README.md): deterministic light and dark screenshot proof.
- [`docs/build/layout.md`](docs/build/layout.md): the source tree.
- [`docs/packaging/README.md`](docs/packaging/README.md): installers, signing and verification.

liquify is part of the liquid project.

## Repository rules

[`CLAUDE.md`](CLAUDE.md) is the instruction file for every contributor and agent: the recorded
decisions, the one-translation-unit rule for SDL and ImGui, the liquid pin, what never changes,
and the full local proof to run before finishing a change. [`AGENTS.md`](AGENTS.md) restates the
non-negotiables for other agents.

## Licence and contributing

liquify is released under the MIT licence by OramaSearch, Inc.; see `LICENSE` and `COPYRIGHT`.
Components written by others are listed with their licences in `THIRD_PARTY_LICENSES`. To
contribute, read [`CONTRIBUTING.md`](CONTRIBUTING.md): every contributor signs the
[Contributor License Agreement](CLA.md) once. Report a vulnerability privately as
[`SECURITY.md`](SECURITY.md) describes.
