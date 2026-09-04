# liquify

**The IDE for liquid.** Native, cross-platform, one binary. Private for now; open source
eventually, like liquid.

liquid is the tool. lake is where it collects. liquify is where you work.

> **Status: editor shell on a retained GPU text layer with graph-backed C/C++ stub trees.**
> `liquify <path>` opens a liquid repository through the public coroutine API, renders its root
> entries, and opens root blobs into an editable in-memory buffer; save writes through libliquid
> as a drip. Unreadable repositories show liquid's error and make `--print-tree` exit non-zero;
> `liquify` with no path retains the hello state, and `--smoke` scrolls and edits 50,000 lines.
> The pin includes liquid's compact-checkpoint repository format, Phase 2 and 3 APIs, and the
> persisted demand-driven query graph and its C/C++ stub-tree query. Stub trees resolve
> declarations from closed files without retaining their full parse trees.
> The specification lives in
> [`oramasearch/liquid`](https://github.com/oramasearch/liquid) under
> [`docs/features/21-liquify-ide.md`](https://github.com/oramasearch/liquid/blob/main/docs/features/21-liquify-ide.md).
> Next, per the bridge's action plan (Phase 4): graph integration after liquid #20 and #21.

## Build and run

Requirements: CMake ≥ 3.28, Ninja, Clang 17+ (Homebrew LLVM on macOS), OpenSSL and zlib. Linux
also needs pkg-config and FUSE3 development files. liquid is a submodule (libliquid is built
in-tree and linked); SDL3, Dear ImGui and GoogleTest are fetched by CMake; SDL is linked static
so the result is one binary.

Every push and pull request repeats the clean-clone build on Linux, macOS, and Windows, then
runs the tests, version command, and display-backed smoke self-test. See
[`docs/build/ci.md`](docs/build/ci.md) for the runner toolchains and dependency cache.

```sh
git clone --recurse-submodules git@github.com:oramasearch/liquify.git
cd liquify
git submodule update --init --recursive
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/liquify            # open the IDE
./build/liquify <path>     # open a liquid repository and render its root tree
./build/liquify --print-tree <path>
./build/liquify --smoke    # scroll/edit 50,000 lines for 30 frames, report latency, exit 0
./build/liquify version
```

## Layout

| Path | Role |
|---|---|
| `include/liquify/app.hpp`, `src/app.cpp` | the application shell: window, renderer, ImGui context, frame loop. **The one TU that includes SDL3 or ImGui.** |
| `src/editor_buffer.hpp`, `src/editor_buffer.cpp` | the in-memory text buffer, input modes, and libliquid blob read/write path |
| `src/text_layout.hpp`, `src/text_layout.cpp` | retained per-blob line layout with edit-local invalidation and UTF-8/tab positioning |
| `src/repository_view.hpp`, `src/repository_view.cpp` | the plain C++ repository loader: runs libliquid blocking calls on a worker and exposes root entries or an error |
| `include/liquify/version.hpp`, `src/version.cpp` | version |
| `src/main.cpp` | CLI: optional repository path, `--print-tree`, `version`, `--smoke [N]`, `--help` |
| `tests/` | GoogleTest; `ctest --test-dir build` |
| `modules/liquid` | **libliquid**, pinned as a submodule; built library-only (`LIQUID_BUILD_CLI OFF`), linked as `liquid::liquid` |

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

## Internal documentation

[`docs/`](docs/) — one folder per topic; every change lands its doc there in the same change.

## Where this sits

Part of the liquid project. See
[`oramasearch/liquid-bridge`](https://github.com/oramasearch/liquid-bridge) for the
cross-module map and the action plan.
