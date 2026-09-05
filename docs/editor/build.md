# Inline build, test and deploy

**What.** The editor keeps build, test and deploy state in `EditorGraphSnapshot`. The panel shows
queued, running-local, running-remote, hit, done and failed actions as liquid publishes them. A
critical-path update marks the contributing action and shows its recorded scheduler reason. Failed
action diagnostics retain liquid's file, line, column, severity and message, so they appear both in
the panel and on the affected retained text line.

**Build.** F7 calls `daemon::build(build_target{})` on the daemon already attached to the open
repository. Before starting it requests `daemon::build_plan(build_target{})`, which supplies the
configured actions, their dependency ordering and their validated hit state. The editor does not
start the `liquid` executable and does not reconstruct action inputs. The returned `build_handle`
is consumed as an awaitable event sequence. A save during a build is captured by the daemon; the
next plan retains unchanged actions as hits.

**Tests.** `repository::tests()` supplies the listed targets. The All tests control uses
`repository::run_tests()` and each target's Run control passes the target name as its filter. A
result says `hit` when liquid reused the recorded action and `ran` when an observed input required
execution. Failed test output remains alongside its result.

**Deploy.** F9 opens the repository's configured remote through `sync_endpoint` and pushes saved
drips with `sync_endpoint_options::atomic` set to false. The panel reports transmitted object and
byte totals, then every `sync_ref_outcome`. A refused reference shows the remote's reason; applied
references remain visible when another reference is refused.

**Bindings.** F7 builds, F8 runs all tests, and F9 deploys. Primary-modifier+P opens the command
palette, which lists the same three commands. The status line reports the running-action count and
the latest build summary.

**Concurrency.** UI input only queues a request. Build subscriptions, test execution and deploy
use liquid's Capy coroutine runtime; snapshots are copied under a mutex before rendering. No editor
build or deploy work executes on the UI thread.

## Headless measurement

`liquify --measure-build PATH [--target TARGET]` exercises the build and test panel controller
without initializing SDL or opening a window. It opens `PATH` through the in-process daemon, adds a
comment inside liquid's `src/store/faults.cpp::visit_fault`, saves it through the configured build
repository, starts `daemon::build(target)`, and consumes that build handle's event sequence. It then
uses the same controller to list tests and run one target through `repository::run_tests(filter)`.
The command writes three JSON records: the build record has save-to-first-action and
save-to-build-result milliseconds, the test record has its end-to-end milliseconds, and the system
record has `/proc/loadavg`. It exits nonzero when the repository is not wrapped, the known source
marker is absent, there are no test targets, or an operation reports an error.

Run the own-tree measurement in liquid's Linux CI image. The image provides FUSE, CMake, Ninja, and
Clang 19. Its disposable container layer installs the X11 development headers required while
liquify configures its vendored SDL3; it does not change the `liquid-linux-ci` image. The
`--privileged` and `--device /dev/fuse` flags are required by `liquid wrap`.

```sh
docker run --rm --privileged --device /dev/fuse \
  -e CC=clang-19 -e CXX=clang++-19 \
  -v "$PWD":/src -w /src liquid-linux-ci bash -lc '
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev
    cmake --fresh -S . -B build-linux-liquify -G Ninja -DLIQUIFY_BUILD_TESTS=OFF
    cmake --build build-linux-liquify
    cmake -S modules/liquid -B modules/liquid/build-linux -G Ninja \
      -DLIQUID_BUILD_CLI=ON
    cmake --build modules/liquid/build-linux --target liquid
    cd modules/liquid
    build-linux/liquid wrap cmake --build build-linux
    cd /src
    SDL_VIDEODRIVER=dummy build-linux-liquify/liquify --measure-build modules/liquid
  '
```

`SDL_VIDEODRIVER=dummy` makes the invocation safe in a headless environment even though this mode
does not create an SDL window.

**Verification.** `EditorGraph.PresentsBuildEventsTestsAndRefusedReferences` covers action state,
a failed action diagnostic placed on a source line, a test hit followed by a rerun, and a refused
reference reason with independent applied references.
