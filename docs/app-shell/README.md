# The application shell

**What.** `liquify::App` (`include/liquify/app.hpp`, `src/app.cpp`): one window, one renderer,
one Dear ImGui context, one frame loop, an activity bar, contextual sidebar, tabbed and split
editor, bottom panel, floating panels and status bar. All built-in surfaces use the plain-C++ panel
registry for metadata, commands, placement, focus, state and notifications
([`../ui/panels.md`](../ui/panels.md)). SDL3 3.2
(static) and Dear ImGui 1.91 use the SDL_Renderer backend. **`src/app.cpp` is the only translation
unit that includes SDL3 or ImGui.** `RepositoryView` (`src/repository_view.hpp`,
`src/repository_view.cpp`) is plain C++ and holds either the root `liquid::tree_entry` values or
an error string. Nested directory trees are requested from libliquid only when their row is
expanded, then published to the panel after the worker completes.

**Decision.** liquify is a native, cross-platform IDE from the start (Steve, 2026-09-02), not an
editor extension. The repository panel is its first Phase 1 client. The editor buffer is a retained
GPU text layer inside Dear ImGui (bridge decision 15); ImGui continues to own chrome and panels.
Diagnostics and the incremental graph follow the liquid #20 and #21 interfaces.

**Lifecycle** (mirrors Zetta `src/gui/app.cpp`): `SDL_Init(VIDEO)` → window at 70 % of the usable
display (floors 900×600), resizable, high-pixel-density → `SDL_CreateRenderer` + vsync → ImGui
context (keyboard nav, no `imgui.ini`, token-derived system/light/dark style and high-density IBM
Plex atlas) → `ImGui_ImplSDL3_InitForSDLRenderer` +
`ImGui_ImplSDLRenderer3_Init`. Each frame: poll events (quit on `SDL_EVENT_QUIT` or the window's
close request), ImGui frame, one full-viewport window, render, present. Shutdown in reverse.

**Repository call path.** `liquify <path>` passes the path through `AppOptions` to
`RepositoryView::open`. That function always launches a `std::async(std::launch::async)` worker;
only its private worker function calls `liquid::repository::open` and `repository::root` through
`liquid::runtime::block_on`. The caller waits before the SDL/ImGui lifecycle begins. The initial
worker reads only the shallow root tree. Expanding a directory starts another worker that reopens
the repository and reads that entry's tree digest; the render thread polls the result and never
blocks on a liquid task. The panel reports the measured repository-open duration. Every liquid
include is under `include/liquid/`; no private liquid header is used. An open or root-read failure
retains liquid's context and message in the view instead of representing the repository as an
empty tree. The panel renders that error, and `--print-tree` writes it to standard error and exits
non-zero.

**CLI.** `liquify` opens the hello state; `liquify <path>` opens the repository panel and lets a
blob at any expanded depth be opened in the editor. C and C++ buffers add graph-query controls, an
inline diagnostics layer, diagnostics/completion and configured plugin panels, plugin commands in
the command palette, and a daemon status line;
`liquify --lake <repository-url> <path>` adds the review panel and reads its bearer from
`LIQUID_TOKEN`;
`liquify --print-tree <path>` prints name, kind and mode without a display;
`liquify --apply-edits <file> <path>` applies canonical-CBOR structural edits without a display
and writes the result record to standard output
([`../editor/structural-editing.md`](../editor/structural-editing.md));
`liquify --screenshot <state> --out <png>` renders one named, fixed shell state through SDL's
dummy video driver and software renderer ([`../ui/README.md`](../ui/README.md),
[`../ui/app-shell.md`](../ui/app-shell.md)); `liquify --smoke [N]` scrolls and edits a generated
50,000-line buffer for N frames (default 30), prints latency metrics, and exits 0; `liquify
version` prints `liquify <v> (libliquid <v>)`. The complete build, test and run sequence is
[`../build/local.md`](../build/local.md).

**libliquid.** liquid is a submodule at `modules/liquid`, built library-only
(`LIQUID_BUILD_CLI OFF`, `LIQUID_BUILD_TESTS OFF`, `EXCLUDE_FROM_ALL`) and linked as
`liquid::liquid`. The pin is `98414c240994`: liquid Phase 1 plus its Phase 2 build engine, Phase 3
distribution foundation, compact shallow repository checkpoints, the persisted demand-driven
query graph from liquid #20, tree-diff and record-enumeration APIs, and the per-checkout daemon and
cursor fan-out from liquid #21. It also includes liquid #65's registered frontend modules and
per-definition C/C++ graph actions, text-anchor mapping, the coroutine-native forge review client
and test double, the format registry, structural editing, hot updates, and relocation-checked
install trees. It carries the shared `liquify_plugin_v1` descriptor merged in liquid PR #239,
build plans and repository builds with public build event streams, graph test actions,
per-reference sync outcomes, streaming source import, flow control, per-line attribution,
repository presence, and operation-log undo. Checkpoints written before
the automerge-cpp adoption in liquid #136 do not load. liquid has its own Capy, Corosio and
automerge-cpp submodules, so
checkouts and pin updates must initialize submodules recursively. The
pin is not a working copy: change liquid in its own repo, push, then bump here in one commit naming
the liquid change it takes:

```sh
git -C modules/liquid fetch origin && git -C modules/liquid checkout <sha>
git add modules/liquid
git submodule update --init --recursive
cmake --build build && ./build/liquify --smoke && ctest --test-dir build
git add modules/liquid && git commit -m "Bump liquid to <sha>: <why>"
```

**Tests.** Repository and editor fixtures are created at test time by the pinned liquid, then
written and reopened through the same public API. A separate corrupt-marker case proves that the
liquid error reaches the view and that `--print-tree` exits non-zero with that message. Buffer and
layout-cache unit tests cover editing, UTF-8 cursor movement, save, reuse and line-local
invalidation. No repository fixture bytes are committed.
Plugin tests cover configured global and repository paths; a bad module remains a visible load
failure while later valid modules register normally.
Panel-registry and app-shell state tests cover docking, focus, progress, cancellation, reset,
repository-local round trips, version 1 migration and invalid state. Screenshot tests read the
in-tree PNG output and inspect stable pixel regions, and `tools/ui/screenshot.sh` renders the full
light/dark state matrix.

**Verify.** `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build &&
./build/liquify --smoke 30 && ./build/liquify --print-tree <repository>`.
