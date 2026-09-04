# The application shell

**What.** `liquify::App` (`include/liquify/app.hpp`, `src/app.cpp`): one window, one renderer,
one Dear ImGui context, one frame loop and a repository root panel. SDL3 3.2 (static) and Dear
ImGui 1.91 use the SDL_Renderer backend. **`src/app.cpp` is the only translation unit that
includes SDL3 or ImGui.** `RepositoryView` (`src/repository_view.hpp`,
`src/repository_view.cpp`) is plain C++ and holds either the root `liquid::tree_entry` values or
an error string.

**Decision.** liquify is a native, cross-platform IDE from the start (Steve, 2026-09-02), not an
editor extension. The repository panel is its first Phase 1 client; editing, diagnostics and the
incremental graph remain Phase 4 work. The open decision is the editor buffer: Dear ImGui text vs
a retained GPU text layer (liquify issue #2).

**Lifecycle** (mirrors Zetta `src/gui/app.cpp`): `SDL_Init(VIDEO)` → window at 70 % of the usable
display (floors 900×600), resizable, high-pixel-density → `SDL_CreateRenderer` + vsync → ImGui
context (keyboard nav, no `imgui.ini`, light style) → `ImGui_ImplSDL3_InitForSDLRenderer` +
`ImGui_ImplSDLRenderer3_Init`. Each frame: poll events (quit on `SDL_EVENT_QUIT` or the window's
close request), ImGui frame, one full-viewport window, render, present. Shutdown in reverse.

**Repository call path.** `liquify <path>` passes the path through `AppOptions` to
`RepositoryView::open`. That function always launches a `std::async(std::launch::async)` worker;
only its private worker function calls `liquid::repository::open` and `repository::root` through
`liquid::runtime::block_on`. The caller waits before the SDL/ImGui lifecycle begins. The render
thread only reads the copied entry values and never blocks on a liquid task. Every liquid include
is under `include/liquid/`; no private liquid header is used.

**CLI.** `liquify` opens the hello state; `liquify <path>` opens the repository panel;
`liquify --print-tree <path>` prints name, kind and mode without a display; `liquify --smoke [N]`
renders N frames (default 30) with no repository and exits 0; `liquify version` prints
`liquify <v> (libliquid <v>)`.

**libliquid.** liquid is a submodule at `modules/liquid`, built library-only
(`LIQUID_BUILD_CLI OFF`, `LIQUID_BUILD_TESTS OFF`, `EXCLUDE_FROM_ALL`) and linked as
`liquid::liquid`. The pin is `fca0612` (liquid Phase 1: repository, store, capture, identity and
VFS). liquid has its own Capy, Corosio and automerge-cpp submodules, so checkouts and pin updates
must initialize submodules recursively. The pin is not a working copy: change liquid in its own
repo, push, then bump here in one commit naming the liquid change it takes:

```sh
git -C modules/liquid fetch origin && git -C modules/liquid checkout <sha>
git add modules/liquid
git submodule update --init --recursive
cmake --build build && ./build/liquify --smoke && ctest --test-dir build
git add modules/liquid && git commit -m "Bump liquid to <sha>: <why>"
```

**Verify.** `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build &&
./build/liquify --smoke 30 && ./build/liquify --print-tree <repository>`.
