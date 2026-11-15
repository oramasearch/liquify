# Inline build, test and push

**What.** The editor keeps build, test and push state in `EditorGraphSnapshot`. The panel shows
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

**Push.** F9 pushes to lake. It is called Push everywhere, as in lake and the liquid CLI (it was
Deploy before #104). The lake it pushes to is, in order:

1. the repository's configured sync remote (`repository::remote_configuration`), when it has one;
2. otherwise the lake repository the folder was cloned from. libliquid's drive client records the
   address a clone came from (`drive_client::open(folder, remote)`), and liquify reads it back with
   `drive_client::open(folder)` and `drive_status::remote` (`recorded_lake_remote`,
   `src/lake_remote.cpp`);
3. otherwise none: F9 shows "This repository has no lake to push to" with a **Set lake** button.

**Set Lake** (the button, or the `run.set-lake` command) asks for the lake repository's address,
copied from its page on lake, and records it the way a clone does (`record_lake_remote`: the drive
client stores it; no network call). A folder that already has a different lake is refused.

The push itself is `sync_endpoint::push` with `atomic` false and the token from `LIQUID_TOKEN`.
The panel reports "Pushed to *host*: *n* objects, *m* bytes", then every `sync_ref_outcome`. A
refused reference shows the lake's reason; applied references remain visible when another reference
is refused. A failure is shown once, by the error component ([`../ui/errors.md`](../ui/errors.md)):
an unreachable lake is offline (saved work stays here), a lake that holds a different repository's
history says so, and the other lake classes keep their own sentences.

**Bindings.** F7 builds, F8 runs all tests, and F9 pushes. Primary-modifier+Shift+P opens the
command palette, which lists the same three commands. The status line reports the running-action
count and the latest build summary.

**Concurrency.** UI input only queues a request. The graph worker consumes the build event sequence
and performs test and push operations, then copies snapshots under a mutex for rendering. The UI
thread never waits for a liquid operation.

## Measurement record

Linux proof deferred (decision 33). The macOS smoke measurement in
[`README.md`](README.md#smoke-measurement) is the current editor measurement. It records retained
rendering and edit-to-present latency, including the display refresh constraint. The build, test,
and push panels remain covered by the editor graph test named below.

**Verification.** `EditorGraph.PresentsBuildEventsTestsAndRefusedReferences` covers action state,
a failed action diagnostic placed on a source line, a test hit followed by a rerun, and a refused
reference reason with independent applied references. The display-required `build` native
acceptance scenario sends F7 and F9 through the application loop against a disposable repository
and requires an honest unavailable daemon and absent-remote state. `opened-clone-push` clones from
a test lake (liquid's `forge_double`) with libliquid, opens the folder in liquify, types, saves,
cuts, presses F9 and requires every reference applied and a fresh clone from the test lake to
serve the edit. `set-lake-push` presses F9 with no lake, clicks **Set lake**, types the address
and requires the next F9 to reach that lake. `front-door-clone-push` does the whole loop from the
front door's clone; it is `WILL_FAIL` until liquify#97 (liquid#526), because the first save into a
store liquify itself just cloned loses an index run. Successful and failed sandboxed
actions remain owed on macOS until macFUSE is available; see
[`../ui/native-acceptance.md`](../ui/native-acceptance.md).
