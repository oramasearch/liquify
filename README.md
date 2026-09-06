# liquify

**The IDE for liquid.** Native, cross-platform, one binary. Private for now; open source
eventually, like liquid.

liquid is the tool. lake is where it collects. liquify is where you work.

> **Status: retained GPU editor with graph-backed analysis, review in place, and optional AI.**
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
> validation backlog in its status line. With `--lake`, the review panel pulls lake changes,
> renders retained inline text hunks and conflict values, posts cursor-anchored comments, advances
> their exact, moved, approximate, or deleted positions across revised heads, and updates review
> state. The default-off assistant builds inspectable, byte-budgeted
> context from those graph facts, streams the Anthropic Messages API over pinned Corosio and
> Boost.HTTP, and applies a proposed edit only after user acceptance. Turning it off constructs no
> provider and opens no connection.
> The specification lives in
> [`oramasearch/liquid`](https://github.com/oramasearch/liquid) under
> [`docs/features/21-liquify-ide.md`](https://github.com/oramasearch/liquid/blob/main/docs/features/21-liquify-ide.md).
> Next, per the bridge's action plan (Phase 4): inline build, test and deploy (liquify #6).

## Build and run

Requirements: CMake ≥ 3.28, Ninja, Clang 17+ (Homebrew LLVM on macOS), OpenSSL and zlib. Linux
also needs pkg-config and FUSE3 development files. liquid is a submodule (libliquid is built
in-tree and linked); Corosio and Boost.HTTP are separately pinned assistant transport submodules;
SDL3, Dear ImGui and GoogleTest are fetched by CMake; SDL is linked static so the result is one
binary.

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
./build/liquify --lake <repository-url> <path> # add lake reviews; bearer is LIQUID_TOKEN
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
| `src/repository_view.hpp`, `src/repository_view.cpp` | the plain C++ repository loader: reads the root on a worker, loads nested trees on expansion, and exposes entries, open latency, or an error |
| `include/liquify/version.hpp`, `src/version.cpp` | version |
| `include/liquify/stub_tree.hpp`, `src/stub_tree.cpp` | tree-sitter C/C++ declaration stubs, their liquid graph query, and cross-file stub resolver |
| `include/liquify/interactive_analysis.hpp`, `src/interactive_analysis.cpp` | open-buffer incremental trees and registered cursor, completion, hover, reference and diagnostics queries |
| `include/liquify/editor_graph.hpp`, `src/editor_graph.cpp` | background editor-query runtime, daemon host/attach lifetime, asynchronous query snapshots and status data |
| `include/liquify/review.hpp`, `src/review.cpp` | coroutine-native review model/controller: forge listing and state, sync pull, text diff, cursor comments, and anchor advancement |
| `include/liquify/context_builder.hpp`, `src/context_builder.cpp` | byte-budgeted, inspectable assistant context assembled from graph facts |
| `include/liquify/ai_provider.hpp`, `src/ai_provider.cpp` | streaming provider interface and Anthropic Messages implementation |
| `include/liquify/ai_config.hpp`, `src/ai_config.cpp` | default-off repository/global configuration and credential lookup |
| `include/liquify/assistant.hpp`, `src/assistant.cpp` | assistant state, proposal acceptance, and local assisted-edit record |
| `src/main.cpp` | CLI: optional repository path, `--lake URL`, `--print-tree`, `version`, `--smoke [N]`, `--help` |
| `tests/` | GoogleTest; `ctest --test-dir build` |
| `modules/liquid` | **libliquid**, pinned as a submodule; built library-only (`LIQUID_BUILD_CLI OFF`), linked as `liquid::liquid` |
| `modules/corosio`, `modules/http` | pinned TLS coroutine and HTTP/1.1 transport used only by the assistant provider |
| `modules/tree-sitter*` | pinned tree-sitter runtime plus generated C and C++ grammars |

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
