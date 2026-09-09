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
is consumed as an awaitable event sequence. The panel renders each planned dependency edge. A
critical action includes liquid's scheduling reason and local and remote estimates. A save during a
build is captured by the daemon; the
editor immediately requests a new plan; unchanged actions remain visible as hits in that plan.

**Tests.** `repository::tests()` supplies the listed targets. The All tests control uses
`repository::run_tests()` and each target's Run control passes the target name as its filter. A
result says `hit` when liquid reused the recorded action and `ran` when an observed input required
execution. Failed test output remains alongside its result.

**Deploy.** F9 opens the repository's configured remote through `sync_endpoint` and pushes saved
drips with `sync_endpoint_options::atomic` set to false. The panel reports transmitted object and
byte totals, then every `sync_ref_outcome`. A refused reference shows the remote's reason; applied
references remain visible when another reference is refused.

**Bindings.** F7 builds, F8 runs all tests, and F9 deploys. Primary-modifier+Shift+P opens the
command palette, which lists the same three commands. The status line reports the running-action
count and the latest build summary.

**Concurrency.** UI input only queues a request. The graph worker consumes the build event sequence
and performs test and deploy operations, then copies snapshots under a mutex for rendering. The UI
thread never waits for a liquid operation.

## Measurement record

Linux proof deferred (decision 33). The macOS smoke measurement in
[`README.md`](README.md#smoke-measurement) is the current editor measurement. It records retained
rendering and edit-to-present latency, including the display refresh constraint. The build, test,
and deploy panels remain covered by the editor graph test named below.

**Verification.** `EditorGraph.PresentsBuildEventsTestsAndRefusedReferences` covers action state,
a failed action diagnostic placed on a source line, a test hit followed by a rerun, and a refused
reference reason with independent applied references. The display-required `build` native
acceptance scenario sends F7 and F9 through the application loop against a disposable repository
and requires an honest unavailable daemon and absent-remote state. Successful and failed sandboxed
actions remain owed on macOS until macFUSE is available; see
[`../ui/native-acceptance.md`](../ui/native-acceptance.md).
