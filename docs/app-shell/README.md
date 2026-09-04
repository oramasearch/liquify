# The application shell

**What.** `liquify::App` (`include/liquify/app.hpp`, `src/app.cpp`): one window, one renderer,
one Dear ImGui context, one frame loop. SDL3 3.2 (static) and Dear ImGui 1.91 with the
SDL_Renderer backend, fetched by CMake; the stack Zetta's headed mode proves on macOS, Windows and
Linux. **`src/app.cpp` is the only translation unit that includes SDL3 or ImGui.**

**Decision.** liquify is a native, cross-platform IDE from the start (Steve, 2026-09-02), not an
editor extension. The shell exists now; everything that makes liquify different (instant open at
any size, diagnostics from the graph, drips, multiplayer) arrives with liquid's store, graph and
daemon (Phases 1 and 4). The open decision is the editor buffer: Dear ImGui text vs a retained
GPU text layer (liquify issue #2).

**Lifecycle** (mirrors Zetta `src/gui/app.cpp`): `SDL_Init(VIDEO)` → window at 70 % of the usable
display (floors 900×600), resizable, high-pixel-density → `SDL_CreateRenderer` + vsync → ImGui
context (keyboard nav, no `imgui.ini`, light style) → `ImGui_ImplSDL3_InitForSDLRenderer` +
`ImGui_ImplSDLRenderer3_Init`. Each frame: poll events (quit on `SDL_EVENT_QUIT` or the window's
close request), ImGui frame, one full-viewport window, render, present. Shutdown in reverse.

**CLI.** `liquify` opens the IDE; `liquify --smoke [N]` renders N frames (default 30) and exits 0,
the display-required self-test; `liquify version` prints `liquify <v> (libliquid <v>)`.

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
./build/liquify --smoke`.
