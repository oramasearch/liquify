# Continuous integration

**What.** `.github/workflows/ci.yml` builds and tests liquify on every push and pull request.
The matrix uses `ubuntu-24.04`, `macos-15`, and `windows-2022`; any failed configure, build,
test, version, or smoke command fails its platform job and the workflow.

**Checkout.** `actions/checkout` uses `submodules: recursive` and the organisation-level
`ORAMA_SUBMODULES_TOKEN` secret, so the pinned private liquid checkout is present. The secret is
a fine-grained token with read access to the organisation's private repositories. Steve creates
and maintains it; this workflow neither creates nor changes the token. Until Steve adds the
secret, a run fails during checkout, before the build commands. The root build keeps
`LIQUID_BUILD_CLI` and `LIQUID_BUILD_TESTS` off and links the library target, which exercises the
same library-only submodule configuration as a local build.

**Dependencies.** SDL3 3.2, Dear ImGui 1.91.5, and GoogleTest remain CMake `FetchContent`
dependencies. liquid adds nested Capy, Corosio and automerge-cpp submodules; its exported target
links Capy publicly. OpenSSL supplies Corosio's TLS implementation, zlib supplies automerge-cpp's
compression support, and Linux installs FUSE3 for liquid's VFS. `actions/cache` stores only
`build/_deps`; each platform has its own key containing the hash of `CMakeLists.txt`. A new key
starts empty and CMake fetches every dependency before building.

## Platform coverage

| Runner | Compiler and display | Verification |
|---|---|---|
| `ubuntu-24.04` | Clang 19 from apt.llvm.org with GCC 14's standard library headers; OpenSSL, FUSE3, SDL's static X11 and Wayland build dependencies; Xvfb | configure, build, `ctest`, `liquify version`, `xvfb-run -a ./build/liquify --smoke 30` |
| `macos-15` | Homebrew's current `llvm` and `openssl@3`; native runner display | configure, build, `ctest`, `liquify version`, `./build/liquify --smoke 30` |
| `windows-2022` | Chocolatey's newest available LLVM and OpenSSL after entering the Visual Studio x64 developer environment; native runner display | configure, build, `ctest`, `liquify version`, `liquify --smoke 30` |

The workflow prints the selected compiler version on every runner. Windows uses `clang-cl` with
the Visual Studio standard library and SDK so C++26 `<print>` is checked with the newest LLVM
package available to that image.

## Local equivalent

With a display available, run the same project-level proof from the repository root:

```sh
cmake -S . -B build -G Ninja && cmake --build build && \
  ctest --test-dir build --output-on-failure && \
  ./build/liquify version && ./build/liquify --smoke 30
```
