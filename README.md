# liquify

**The IDE for liquid.** Native, cross-platform, one binary. Private for now; open source
eventually, like liquid.

liquid is the tool. lake is where it collects. liquify is where you work.

> **Status: hello world.** `liquify` opens a window on SDL3 + Dear ImGui, prints its version,
> and can self-test by rendering a fixed number of frames. The specification lives in
> [`oramasearch/liquid`](https://github.com/oramasearch/liquid) under
> [`docs/features/21-liquify-ide.md`](https://github.com/oramasearch/liquid/blob/main/docs/features/21-liquify-ide.md).
> Next, per the bridge's action plan (Phase 4): the editor surface backed by liquid's query
> graph and virtual filesystem.

## Build and run

Requirements: CMake ≥ 3.28, Ninja, Clang 17+ (Homebrew LLVM on macOS). SDL3, Dear ImGui and
GoogleTest are fetched by CMake; SDL is linked static so the result is one binary.

```sh
git clone git@github.com:oramasearch/liquify.git
cd liquify
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/liquify            # open the IDE
./build/liquify --smoke    # render 30 frames and exit 0 (needs a display)
./build/liquify version
```

## Layout

| Path | Role |
|---|---|
| `include/liquify/app.hpp`, `src/app.cpp` | the application shell: window, renderer, ImGui context, frame loop. **The one TU that includes SDL3 or ImGui.** |
| `include/liquify/version.hpp`, `src/version.cpp` | version |
| `src/main.cpp` | CLI: `version`, `--smoke [N]`, `--help` |
| `tests/` | GoogleTest; `ctest --test-dir build` |

## What this is

A native editor that shares memory with liquid's graph instead of serialising across a
process boundary. That is the whole point, and it is why it is not an extension for someone
else's editor. What it does that others cannot, once the store, graph and daemon exist:

| | |
|---|---|
| Instant open | any repository at any size, no indexing wait — the virtual filesystem and the warm graph |
| Diagnostics without a second parse | from the same graph the build uses |
| Drips | every save is a signed, attributed change; history is a query |
| Multiplayer | people and agents in one buffer, attributed via TAAP |

It starts from the window and UI stack Zetta's headed mode already proves on macOS, Windows
and Linux (SDL3 + Dear ImGui). No Zetta code is used, only its choices; liquify never depends
on Zetta.

## Where this sits

Part of the liquid project. See
[`oramasearch/liquid-bridge`](https://github.com/oramasearch/liquid-bridge) for the
cross-module map and the action plan.
