# Implementation status

What the source tree does today, by area, with the page that holds each area's contract.
`CLAUDE.md` carries the same status in the form agents read first; when an area changes, amend
both in the same change. The specification lives in
[`oramasearch/liquid`](https://github.com/oramasearch/liquid) under
[`docs/features/21-liquify-ide.md`](https://github.com/oramasearch/liquid/blob/main/docs/features/21-liquify-ide.md).

In one line: a retained GPU editor with graph-backed interactive C/C++ analysis, inline build,
test and deploy, structural editing, review in place, native plugins, and an optional AI surface,
over one pinned libliquid.

## Shell and repository

`liquify <path>` opens a liquid repository through the public coroutine API, off the render
thread, renders its root entries, loads nested trees on expansion, and opens blobs into an
editable in-memory buffer; save writes through libliquid as a drip. Unreadable repositories show
liquid's error and make `--print-tree` exit non-zero; `liquify` with no path retains the hello
state. The standard input mode is non-modal and a modal command/insert mode is available.
`--smoke` scrolls and edits a 50,000-line buffer, reports frame and typing latency, and exits 0
([`../app-shell/README.md`](../app-shell/README.md), [`../editor/README.md`](../editor/README.md)).
The stamped Orama token and IBM Plex snapshot under `design/` generate committed native constants;
its gate verifies source hashes and rejects stale generated output. Those roles drive the system,
light and dark shell with its activity bar, contextual sidebar, tabbed and split editor, bottom
panel and status bar. Divider sizes, visibility and selected surfaces persist per repository;
macOS installs native command menus and Cmd bindings
([`../ui/design-system.md`](../ui/design-system.md), [`../ui/app-shell.md`](../ui/app-shell.md)).
`--screenshot` renders the fixed welcome, repository, C++ editor and panel registry in light and
dark through a headless software renderer; the committed image set and pixel tests are described
in [`../ui/README.md`](../ui/README.md).

## The liquid pin

liquid is pinned at `modules/liquid`. The pin includes liquid's compact-checkpoint repository
format, Phase 2 and 3 APIs, the persisted demand-driven query graph, tree-diff and
record-enumeration APIs, the per-checkout daemon, registered frontend modules with per-definition
C/C++ actions, the C/C++ stub-tree query, text-anchor mapping, and the coroutine-native forge
review client used by review-in-place workflows. The exact commit and what it carries are in
[`../app-shell/README.md`](../app-shell/README.md).

## Interactive analysis

C and C++ stubs are graph query values that resolve declarations from closed files without
retaining full parse trees ([`../stub-trees/README.md`](../stub-trees/README.md)). Open overlays
retain error-tolerant incremental trees and answer node, enclosing-definition, definition,
reference, hover, completion and build-diagnostic queries; an edit cancels their in-flight graph
work. Graph-backed editor queries resolve through closed-file stubs or compiler symbol records and
read build diagnostics by translation unit. The editor renders graph diagnostics inline and in a
panel, exposes definition, hover and completion controls, and shows daemon drip triggers plus the
validation backlog in its status line
([`../editor/interactive-analysis.md`](../editor/interactive-analysis.md)).

## Structural editing

Syntax-node selection, cross-file rename and C/C++ extract-function are serialized on the editor
worker and preview fingerprinted node edits before applying one drip per changed file. The same
canonical CBOR records are accepted by the headless `--apply-edits` command. This is the local
adapter for bridge decision 30 until libliquid owns the permanent structural edit operation
([`../editor/structural-editing.md`](../editor/structural-editing.md)).

## Review in place

With `--lake`, the review panel pulls lake changes through liquid, renders retained inline text
hunks and conflict values, posts cursor-anchored comments, advances their exact, moved,
approximate, or deleted positions across revised heads, and updates review state
([`../editor/review.md`](../editor/review.md)).

## Assistant

The default-off assistant builds inspectable, byte-budgeted context from the same graph facts,
streams the Anthropic Messages API over the pinned Corosio and Boost.HTTP transport, and applies a
proposed edit only after user acceptance, as one normal drip. Turning it off constructs no provider
and opens no connection ([`../editor/ai.md`](../editor/ai.md)).

## Native plugins

Native plugins use liquid's versioned frontend C boundary to register graph queries, commands,
and canonical-CBOR panels through checked static or configured dynamic loading. Plugin commands
appear in the palette and data-only panels render below analysis results. The sample plugin
proves all three registrations; load and panel failures remain local to their source
([`../plugins/README.md`](../plugins/README.md)).

## Build, test and deploy

F7 plans and starts the configured build through the attached daemon, then renders its action
event stream, critical path and scheduler decisions. F8 lists and runs graph test actions. F9
pushes saved drips to the configured sync remote and reports every reference outcome, including
an authorization refusal while accepted references continue. Failed action diagnostics render on
their affected editor lines ([`../editor/build.md`](../editor/build.md)).

## Packaging and CI

Every main-branch push and pull request repeats the clean-clone release build on Linux, macOS and
Windows, runs the tests, the version command and the display-backed smoke self-test, and produces
a hardened-runtime macOS `pkg` and `dmg`, a signed and install-tested Windows MSIX, and an AppImage
exercised on Ubuntu and Fedora. `v*` tags publish those installers with signed and unsigned
SHA-256 records; notarization runs when the release secrets are present. Hand-built packaging
remains in place until liquid's projection and platform-signing targets land
([`../build/ci.md`](../build/ci.md), [`../packaging/README.md`](../packaging/README.md)).
