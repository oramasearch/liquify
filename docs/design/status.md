# Implementation status

What the source tree does today, by area, with the page that holds each area's contract.
`CLAUDE.md` carries the same status in the form agents read first; when an area changes, amend
both in the same change. The specification lives in
[`oramasearch/liquid`](https://github.com/oramasearch/liquid) under
[`docs/features/21-liquify-ide.md`](https://github.com/oramasearch/liquid/blob/main/docs/features/21-liquify-ide.md).

In one line: a retained GPU editor with graph-backed interactive C/C++ analysis, inline build,
test and push, structural editing, review in place, native plugins, and an optional AI surface,
over one pinned libliquid.

## Shell and repository

With no path, the shell is a repository front door: open a local folder, create a repository,
clone through liquid's drive client, reopen one of eight persisted recent repositories, show Your
identity (this computer's TAAP signing identity, not a lake sign-in), or create another
process-backed window. It is laid out as lake's pages: a column with the title and the ways in as
large action rows, and Recent Repositories as a compact table, with a teaching empty state
([first run](../ui/first-run.md#layout)). Opening a different repository replaces its
services in the current process. Unreadable local repositories and unreachable lake remotes have
separate retry states. A failed clone is classified from liquid's error code: only an unreachable
lake is offline, and a lake that answered (TLS, token, access, not found, busy, unsupported) gets its
own sentence, next step and Details ([`../ui/first-run.md`](../ui/first-run.md),
[`../ui/errors.md`](../ui/errors.md)).

`liquify <path>` opens a liquid repository through the public coroutine API, off the render
thread, renders its root entries, loads nested trees on expansion, and opens blobs into an
editable in-memory buffer per path; switching retains text, selection and local undo. Native
Save / Discard / Cancel dialogs protect close, repository replacement and quit
([`../ui/buffers.md`](../ui/buffers.md)); save writes through libliquid as a drip. Unreadable repositories show
liquid's error, adding upgrade guidance for unsupported formats, and make `--print-tree` exit non-zero. A background index classifies every path,
adds libliquid drive and repository change annotations, and retains libliquid symbol records.
Quick Open, Symbol Search, definitions, references, available call relationships, breadcrumbs,
recent locations, history traversal, and repository context actions share one registered default-key
command set ([`../ui/navigation.md`](../ui/navigation.md)). The standard input mode is non-modal and
a modal command/insert mode is available.
`--smoke` scrolls and edits a 50,000-line buffer, reports frame and typing latency, and exits 0
([`../app-shell/README.md`](../app-shell/README.md), [`../editor/README.md`](../editor/README.md)).
SDL composition handles CJK input methods and dead keys without editing until commit. Rich and
plain clipboard forms, file drops through libliquid, complete keyboard traversal with visible
focus, macOS accessibility labels, high contrast, and the OS reduced-motion preference are
recorded in [`../ui/accessibility.md`](../ui/accessibility.md).
The stamped Orama token and IBM Plex snapshot (Sans Regular and SemiBold, Mono) under `design/`
generate committed native constants, and a title / heading / body / caption type scale is applied
to the front door, dialogs, panel headers and the empty editor; controls follow lake's kit (one primary action
per surface, secondary and quiet buttons, labelled checkboxes that show their state, labels above
full-width fields, one slider shape); shell regions are flat and share edges separated by hairlines, with
a compact status bar and a translucent canvas scrim behind dialogs;
its gate verifies source hashes and rejects stale generated output. Those roles drive the system,
light and dark shell with its activity bar, contextual sidebar, tabbed and split editor, bottom
panel, floating panels and status bar. One frame-level drawable scale applies to shell and
retained editor logical coordinates; retained drawing respects ImGui popup order. Native GPU
window regression covers resize, fonts, hit testing and occlusion ([the coordinate
contract](../ui/app-shell.md#logical-coordinates-and-native-pixels)). Native workflow acceptance
opens an unmodified desktop window, drives a disposable libliquid repository through SDL events,
and records known product defects separately from unavailable display modes
([`../ui/native-acceptance.md`](../ui/native-acceptance.md)). One registry supplies panel
metadata, commands, placement, focus, state and notifications; each surface moves among dock
areas, floats and re-docks. Divider
sizes and complete panel layouts persist per repository and can be reset. Bottom-panel tabs have
disjoint hit rectangles sized from the UI font, so pointer and keyboard focus agree at every size
(liquify#35). The status bar reports
actionable build, sync, hydration and failure state, its daemon state from the graph's snapshot of
the build service (a startup failure and a lost connection included), and its row height and
segment priorities from the UI font and the width (liquify#36); a failure is shown once, by one error
component (sentence, next step, action, Details, no absolute paths), in the panel the status bar
names ([`../ui/errors.md`](../ui/errors.md)). Collaboration, identity, change, timing and
validation records remain available through explicit gutter, panel, and details controls. macOS installs native
command menus and Cmd bindings ([`../ui/design-system.md`](../ui/design-system.md),
[`../ui/app-shell.md`](../ui/app-shell.md), [`../ui/panels.md`](../ui/panels.md)).
The shared command registry powers fuzzy palette search, recent commands, platform key bindings,
keymap conflict reporting, native menus and headless `--command` invocation
([`../ui/commands.md`](../ui/commands.md)). It is the only key path: window keys, palette rows and
native menu items run one handler table, navigation actions are registry commands, and a rebind
in Settings reaches keys, palette and menus at once (liquify#34). Cut Change (palette, Change menu, Primary+Shift+K)
names the drips saved since the last change as one change through libliquid's cut API, after
showing what it gathers ([`../editor/cut.md`](../editor/cut.md)). While a modal dialog is open,
typed text reaches only the dialog, never the source behind it. F9 is Push: it pushes to the
repository's sync remote or the lake it was cloned from, and with neither offers Set Lake
([`../editor/build.md`](../editor/build.md)). Request Review asks lake to review the current change;
the Review panel uses the repository's lake, teaches when it is empty, and never names a
command-line flag ([`../editor/review.md`](../editor/review.md)). F7 on a repository with no
recorded build offers Set up build, which proposes a command from the repository and records it
through libliquid's wrap API ([`../editor/build.md`](../editor/build.md)).
The searchable settings surface reads a user `.liquid` file plus repository overrides and applies
theme, fonts, editor behavior, keymaps, optional AI, lake accounts and default-off telemetry live.
Parser and typed validation errors remain inline, group reset is local, and credential values never
enter the settings model ([`../ui/settings.md`](../ui/settings.md)).
`--screenshot` renders the fixed welcome, recovery, repository, navigation, C++ editor, panel
registry and settings states in light and
dark through a headless software renderer; the committed image set and pixel tests are described
in [`../ui/README.md`](../ui/README.md).

The editor supports multi-cursor and rectangular edits, repository-configured indentation,
soft-wrap and fold row models, liquid tree-sitter syntax, bracket matching, line/diagnostic/
attribution/presence/Drive gutters, buffer and repository search/replace, per-buffer undo/redo,
and repository operation history. Its interaction, worker and screenshot contracts are in
[`../ui/editor.md`](../ui/editor.md).

## The liquid pin

liquid is a sibling checkout, resolved here through `LIQUIFY_LIQUID_SOURCE_DIR`
(default `../liquid`).
An independent CLI → IDE → CLI integration check exercises repository creation, file listing,
editor save, read-back, and unsupported-format guidance; the artifact combinations are in
[`../build/store-compatibility.md`](../build/store-compatibility.md). The pin includes liquid's compact-checkpoint repository
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
panel, exposes definition, hover and completion controls, and makes daemon drip triggers plus the
validation backlog available from Diagnostics details
([`../editor/interactive-analysis.md`](../editor/interactive-analysis.md)).

## Structural editing

Syntax-node selection, cross-file rename and C/C++ extract-function are serialized on the editor
worker and preview fingerprinted node edits before applying one drip per changed file and one
grouped liquid operation. The same canonical CBOR records are accepted by the headless
`--apply-edits` command and translated to liquid's public edit API
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
([`../plugins/README.md`](../plugins/README.md)). Plugins named by repository metadata load only
when the user has trusted their SHA-256 digest with `liquify plugin trust`, from a per-user trust
store outside every repository; otherwise they are listed as not trusted and never loaded. Every
dynamic plugin runs in its own sandboxed `liquify-plugin-host` process and the IDE never maps its
library; a crash or hang fails only the call that met it, and a plugin is disabled after three
restarts. Panel refreshes, commands and events are sent from one worker per plugin, so the window
never waits for a plugin: a panel draws its last answer or its loading state until the next answer
arrives (liquify#149). Limits: reads are not denied, and Windows has no network sandbox.

## Build, test and push

F7 plans and starts the configured build through the attached daemon, then renders its action
event stream, critical path and scheduler decisions. F8 lists and runs graph test actions. F9
pushes saved drips to the configured sync remote and reports every reference outcome, including
an authorization refusal while accepted references continue. Failed action diagnostics render on
their affected editor lines ([`../editor/build.md`](../editor/build.md)).

## Packaging and CI

The workflows in `.github/workflows/` describe a clean-clone build on Linux, macOS and Windows and
installer packaging (a hardened-runtime macOS `pkg` and `dmg`, a signed Windows MSIX, an
AppImage), but GitHub Actions is turned off for this repository, so none of them runs. Proof is
local: the build and the full suite, run when a release is cut
([`../releases/README.md`](../releases/README.md)). v0.1.0 publishes no installer. Hand-built
packaging remains in place until liquid's projection and platform-signing targets land
([`../build/ci.md`](../build/ci.md), [`../packaging/README.md`](../packaging/README.md)).

Native window and Diagnostics screenshot rendering checks consume the current frame layout
and submitted glyph ink. They assert drawable coverage, text extent, editor/panel occupancy,
and selected-tab visibility; renderer mutations and a moved toolbar verify their signal. See
[`../ui/property-tests.md`](../ui/property-tests.md).
