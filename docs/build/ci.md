# Continuous integration

> **Disabled 2026-11-15.** GitHub Actions are turned off for this repository and every workflow file
> is commented out (no hosted CI in oramasearch repositories). Builds, installers and their checks run
> locally; tests run when a release is cut. What follows describes the workflows as they were.

**What.** `.github/workflows/ci.yml` builds, tests, packages, and verifies liquify on every
main-branch push and pull request. The reusable matrix uses `ubuntu-24.04`, `macos-15`, and
`windows-2022`; any failed configure, build, test, version, smoke, installer construction, or
required installer check fails its platform job. A fourth job downloads the Linux artifact and
runs it on Fedora 42.

**Checkout.** `actions/checkout` uses `submodules: recursive` and the organisation-level
`ORAMA_SUBMODULES_TOKEN` secret, so the liquid checkout is present. The secret is
a fine-grained token with read access to the organisation's private repositories. The maintainers
create and maintain it; this workflow neither creates nor changes the token. Until the secret
exists, a run fails during checkout, before the build commands. The root build keeps
`LIQUID_BUILD_CLI` and `LIQUID_BUILD_TESTS` off and links the library target, which exercises the
same library-only submodule configuration as a local build.

liquify has two additional submodules: `modules/corosio` at
`9f8e865a7b5728a4093e9345456bc077194dbeff` and `modules/http` at
`af0353a5b2868d5b8db7fb01ffb5c464f9deb8a4`. They pin the assistant transport independently of
liquid. The pins match liquid's nested copies, so the single Boost target instances created while
configuring libliquid also compile the provider. No other liquify source contains network code.

**Dependencies.** SDL3 3.2, Dear ImGui 1.91.5, and GoogleTest remain CMake `FetchContent`
dependencies. liquid adds nested Capy, Corosio and automerge-cpp submodules; its exported target
links Capy publicly. OpenSSL supplies Corosio's TLS implementation, zlib supplies automerge-cpp's
compression support, and Linux installs FUSE3 for liquid's VFS. `actions/cache` stores only
`build/_deps`; each platform has its own key containing the hash of `CMakeLists.txt`. A new key
starts empty and CMake fetches every dependency before building. All three runners configure with
`CMAKE_BUILD_TYPE=Release`; the packaged binary is the one that passed that platform's tests,
version command, and smoke run.

## Platform coverage

| Runner | Compiler and display | Verification |
|---|---|---|
| `ubuntu-24.04` | Clang 19 from apt.llvm.org with GCC 14's standard library headers; OpenSSL, FUSE3, SDL's static X11 and Wayland build dependencies; Xvfb | configure, build, `ctest`, version, smoke, digest-pinned AppImage creation and run, Flatpak build-only validation when its runtime installs |
| `macos-15` | Homebrew's current `llvm` and `openssl@3`; native runner display | configure, build, `ctest`, version, AI-off smoke with continuous `lsof` TCP inspection, `pkg` and `dmg` construction, hardened-runtime signing, conditional notarization and Gatekeeper assessment |
| `windows-2022` | Chocolatey's newest available LLVM and OpenSSL after entering the Visual Studio x64 developer environment; native runner display | configure, build, `ctest`, version, smoke, MSIX packing and timestamped signing, signature verification, install, packaged version run |
| Fedora 42 container | Fedora user space on an Ubuntu Actions host | download the Ubuntu-built AppImage and run its `liquify version` command with extraction mode |

The workflow prints the selected compiler version on every runner. Windows uses `clang-cl` with
the Visual Studio standard library and SDK so C++26 `<print>` is checked with the newest LLVM
package available to that image.

The macOS smoke uses the default-off configuration and inspects the process for owned TCP sockets
until it exits. Any socket fails the job. The connection-counting unit test provides the same
invariant without relying on process inspection and runs on every platform.

The Linux job downloads `linuxdeploy-x86_64.AppImage` from release `1-alpha-20251107-1` only after
checking SHA-256 `c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d`.
It collects non-system runtime libraries before creating the artifact. Both the tool and resulting
AppImage run in extraction mode, so neither the Ubuntu nor Fedora check depends on host FUSE
configuration. The AppImage is the release artifact; the Flatpak manifest is a checked second
format.

The Windows no-secret path creates a self-signed code-signing certificate, timestamps the MSIX,
trusts that certificate only for the current runner, installs the package, and invokes the
installed executable. Release secrets replace that certificate on tag builds. The macOS no-secret
path uses an ad hoc hardened-runtime signature and reports notarization as skipped; a complete
Developer ID and App Store Connect secret set makes notarization, stapling, and successful
Gatekeeper assessment required. Credential names and verification commands are documented in
[`docs/packaging/README.md`](../packaging/README.md).

## Tag releases

`.github/workflows/release.yml` runs for `v*` tags and calls `ci.yml` as a reusable workflow. After
all platform jobs and the Fedora check pass, its publication job downloads the installers and
per-platform signed and unsigned digest records, computes and checks aggregate `SHA256SUMS`, and
creates the GitHub release. Only the publication job receives `contents: write`; build jobs remain
read-only.

## Local equivalent

With a display available, run the same project-level proof from the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && \
  ctest --test-dir build --output-on-failure && \
  ./build/liquify version && ./build/liquify --smoke 30
```
