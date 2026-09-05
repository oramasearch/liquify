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

**Verification.** `EditorGraph.PresentsBuildEventsTestsAndRefusedReferences` covers action state,
a failed action diagnostic placed on a source line, a test hit followed by a rerun, and a refused
reference reason with independent applied references.
