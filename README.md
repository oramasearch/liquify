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
off. `design/` carries the stamped Orama DTCG snapshot, generated native roles and embedded IBM
Plex assets. The light/dark application shell hosts every surface through one panel registry;
dock, editor-tab and floating placement plus focus persist per repository, layouts reset to their
defaults, and one channel reports long-operation progress. Native macOS menus remain available.
Pull-request CI packages macOS, Windows and Linux installers and `v*` tags publish them;
[`docs/design/status.md`](docs/design/status.md) lists each area with its page, and the
specification is `oramasearch/liquid` `docs/features/21-liquify-ide.md`.

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
