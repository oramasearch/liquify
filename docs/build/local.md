# Local build, test and run

## Requirements

- CMake 3.28 or newer and Ninja.
- Node.js for the design-token gate when tests are enabled.
- Clang 17 or newer. On macOS `CMakeLists.txt` selects Homebrew LLVM
  (`/opt/homebrew/opt/llvm/bin/clang++`) and the Xcode SDK before `project()`; on Linux it takes
  the newest `clang++-NN` on the path. `-DLIQUIFY_USE_SYSTEM_COMPILER=ON` or an explicit
  `CMAKE_CXX_COMPILER` overrides that.
- OpenSSL (Corosio's TLS) and zlib (automerge-cpp's compression). Linux also needs pkg-config and
  the FUSE3 development files for liquid's VFS.
- Submodules. liquid at `modules/liquid` is built in-tree, library-only, and linked as
  `liquid::liquid`; it carries nested Capy, Corosio and automerge-cpp submodules. Corosio and
  Boost.HTTP at `modules/corosio` and `modules/http` are the separately pinned assistant transport.
  The tree-sitter runtime and its C and C++ grammars are pinned under `modules/tree-sitter*`.
  Clone with `--recurse-submodules`, and run `git submodule update --init --recursive` after any
  pin change.
- SDL3 3.2, Dear ImGui 1.91 and GoogleTest are fetched by CMake into `build/_deps` on the first
  configure. SDL is linked static, so the result is one binary.

CMake options: `LIQUIFY_BUILD_TESTS` (on), `LIQUIFY_BUILD_BENCH` (off; the stub-tree benchmarks
under `bench/`), `LIQUIFY_USE_SYSTEM_COMPILER` (off). The configure step exports
`compile_commands.json`.

## Build

```sh
git clone --recurse-submodules git@github.com:oramasearch/liquify.git
cd liquify
git submodule update --init --recursive
cmake -S . -B build -G Ninja
cmake --build build -j 6
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

CTest holds the GoogleTest cases discovered from `liquify-tests` (buffer, text layout, repository
view, app-shell state, screenshot capture, stub trees, interactive analysis, editor graph,
structural edits, review, assistant, plugins and version), the design-token gate, plus
`liquify-print-tree-unreadable`, which proves that an unreadable repository
makes `--print-tree` exit non-zero with liquid's error. Screenshot cases render the editor and a
panel through SDL's dummy video driver and software renderer and inspect stable pixel regions.
`version`, the unit tests and `--screenshot` run without a display; `--smoke` needs one.

## Run

```sh
./build/liquify            # open the IDE
./build/liquify <path>     # open a liquid repository and render its root tree
./build/liquify --lake <repository-url> <path> # add lake reviews; bearer is LIQUID_TOKEN
./build/liquify --print-tree <path>
./build/liquify --apply-edits request.cbor <path> > result.cbor
LIQUIFY_BINARY=./build/liquify ./tools/ui/screenshot.sh
./build/liquify --smoke    # scroll/edit 50,000 lines for 30 frames, report latency, exit 0
./build/liquify --smoke 300
./build/liquify --screenshot editor-cpp-light --out editor.png
./build/liquify version
```

The commands are described in [`../app-shell/README.md`](../app-shell/README.md); `--apply-edits`
in [`../editor/structural-editing.md`](../editor/structural-editing.md), `--lake` in
[`../editor/review.md`](../editor/review.md), and the `--smoke` measurements in
[`../editor/README.md`](../editor/README.md). The screenshot registry and full refresh command are
in [`../ui/README.md`](../ui/README.md).

## The full local proof

`CLAUDE.md` requires this chain to be green before any change is finished:

```sh
cmake -S . -B build -G Ninja && cmake --build build && \
  ctest --test-dir build --output-on-failure && \
  ./build/liquify version && ./build/liquify --smoke 30
```

## Continuous integration

Every main-branch push and pull request repeats the clean-clone release build on Linux, macOS,
and Windows, runs the tests, version command, and display-backed smoke self-test, and produces the
native installers. `v*` tags publish the verified artifacts and their SHA-256 records. See
[`ci.md`](ci.md) for the runner toolchains and [`../packaging/README.md`](../packaging/README.md)
for signing and verification.
