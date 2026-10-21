# CLAUDE.md — liquify

The IDE for liquid. **Org-wide conventions** live in
[`oramasearch/orama-bridge`](https://github.com/oramasearch/orama-bridge) `CLAUDE.md`
and apply in full. Project conventions live in
[`oramasearch/liquid`](https://github.com/oramasearch/liquid) `CLAUDE.md` — vocabulary,
document rules, confidentiality, and the decisions not to drift past. Read both.

## Status

**Retained GPU editor with indexed repository and graph-symbol navigation, graph-backed interactive C/C++ analysis, multiplayer presence and attribution, lease controls, inline build, test and deploy, structural editing, review in place, native plugins, and optional AI.**
`./build/liquify` opens a repository front door with local open, lake clone, recent repositories,
local TAAP identity and additional-window actions. Repositories switch without restarting.
`./build/liquify <path>` opens a liquid repository off the render thread, shows its root entries,
loads nested trees when expanded, indexes all paths and libliquid symbol records, annotates file
kind plus drive and change state, and opens blobs into retained in-memory buffers keyed by path.
Switching preserves text, selections and local undo; native Save / Discard / Cancel dialogs protect
close, repository replacement and quit (see `docs/ui/buffers.md`). Save calls libliquid and produces
a drip. Quick Open, Symbol Search, definitions, references, call hierarchy,
breadcrumbs, recent locations, and back/forward share one navigation action registry with default
bindings; repository rows expose open, rename, move, new-file, and reveal actions.
The standard input mode is non-modal and a modal command/insert mode is available. Multi-cursor
and column edits, repository indentation, liquid syntax/folds, collaboration gutters,
search/replace, local edit history and operation-log history are part of the editor view. Unreadable
repositories retain liquid's error, with upgrade guidance for unsupported store formats, and `liquify --smoke` scrolls and edits a 50,000-line buffer,
reports frame and typing latency, and exits 0. Unreadable local repositories and unreachable lake
clones have distinct retry states. `liquify --screenshot` renders fixed welcome, recovery,
repository, navigation, settings, C++ editor, complete editor view and panel registry states in
light and dark through a headless software renderer, with committed PNG proof and pixel-region
tests. liquid is consumed as a sibling module (`LIQUIFY_LIQUID_SOURCE_DIR`, default `../liquid`)
through its persisted demand-driven query graph,
text-anchor mapping, syntax modules, attribution, presence, operation history, and coroutine-native
forge review client.
The stamped Orama DTCG snapshot under `design/` generates native light and dark roles plus
embedded IBM Plex Sans and Mono assets; its gate rejects changed source and output bytes. The
light/dark shell arranges the activity bar, contextual sidebar, split editor, bottom panel,
floating panels and status bar from those roles. Shell and retained editor share logical
coordinates with one frame-level drawable scale; native GPU window and workflow acceptance tests
cover drawable coverage, reported editor/panel areas, text extent, popup occlusion, real repository
input and panel selection. The Diagnostics screenshot uses the same layout properties in both themes;
renderer mutations and a moved toolbar verify the assertions (`docs/ui/property-tests.md`). Font texture failures stop rendering with a visible native error. One plain-C++
registry owns every panel's metadata, commands, placement, focus, state and notifications; panels
move among dock areas, float and re-dock, repository-local state persists the complete layout, and
reset restores defaults. One status channel reports build, saved-drip sync, repository hydration
and assistant progress. macOS
exposes the command groups and Cmd bindings through the native menu bar. The searchable settings
surface reads user and repository `.liquid` files, applies appearance, editor, keymap, AI and lake
changes live, keeps telemetry off by default, and never places credential values in its model or
UI. SDL composition supports
CJK input methods and dead keys; rich and plain clipboard forms, file drops, visible keyboard
focus, macOS accessibility labels, high contrast and the OS reduced-motion preference cover the
shell. The status bar reports actionable build, sync, hydration and failure state; collaboration,
identity, change and validation details open only through their explicit affordances. macOS exposes
the command groups and Cmd bindings through the native menu bar. The same registry provides a fuzzy palette with recent commands and resolved
bindings, typed `.liquid` keymap overrides with conflict reporting, plugin commands, and headless
`--command` invocation. The searchable settings surface reads user and repository `.liquid` files,
applies appearance, editor, keymap, AI and lake changes live, keeps telemetry off by default, and
never places credential values in its model or UI. SDL composition supports CJK input methods and
dead keys; rich and plain clipboard forms, file drops, visible keyboard focus, macOS accessibility
labels, high contrast and the OS reduced-motion preference cover the shell. C and C++ stubs
are graph query values that resolve
declarations from closed files without retaining full parse trees. Graph-backed editor queries run
over error-tolerant incremental trees for open overlays, resolve through closed-file
stubs or compiler symbol records, and read build diagnostics by translation unit. The editor
surface renders those diagnostics inline and in a panel, exposes definition, hover and completion
demands, and exposes daemon drip triggers and the validation backlog in Diagnostics details.
Syntax-node selection,
cross-file rename and C/C++ extract-function are serialized on the editor worker and applied as
fingerprinted node edits with one drip per changed file and one liquid operation for the complete
multi-file application. The headless `--apply-edits` command uses the same canonical CBOR records
as the editor and applies them through liquid's public edit API. The `--lake` review panel pulls
changes through liquid, renders retained inline text hunks and conflict values, creates
cursor-anchored comments, advances them across revised heads, and updates review state. The
default-off assistant
assembles inspectable context from the same graph, streams through a pinned provider transport,
and previews edits until the user accepts one normal drip. When off it constructs no provider and
opens no connection. Native plugins use liquid's versioned frontend C boundary to register graph
queries, commands, and canonical-CBOR panels through checked static or configured dynamic loading;
a plugin named by repository metadata loads only once the user has trusted its SHA-256 digest with
`liquify plugin trust` (per-user trust store, never the repository); their commands and data-only panels appear in the editor surface. F7 plans and starts a configured
build through the attached daemon and renders its action stream, critical path and scheduling
choice. F8 lists and runs test actions; F9 pushes saved drips to the configured sync remote and
renders each reference outcome. Failed action diagnostics are line diagnostics in the retained
editor. Pull-request CI packages the release binary as a self-contained, hardened-runtime macOS
`pkg` and `dmg`, a signed and install-tested Windows MSIX, and an AppImage verified on Ubuntu and
Fedora. The macOS build audits every bundled Mach-O for non-system absolute dependencies and
launches a relocated copy from the mounted dmg. Tag releases publish those installers and their
signed and unsigned SHA-256 records; macOS notarization runs when the App Store Connect credentials
are available. Hand-built packaging remains in place until liquid's projection and
platform-signing targets land.

The liquid pin is `7ff6ed82f93b916ef206c5776e2761b19536d8f3`; CLI and IDE revisions plus supported
store formats are displayed by `version`. The independent CLI → IDE → CLI compatibility test
and supported lake revision are recorded in [`docs/build/store-compatibility.md`](docs/build/store-compatibility.md).

## Decisions — recorded

1. **liquify is a native, cross-platform IDE from the start** (Steve, 2026-09-02). Not a VS
   Code extension, not a JetBrains plugin, not Electron. Those may exist later as adjuncts;
   the product is the editor.
2. **Private for now, open source eventually**, like liquid. Same licence decision as liquid
   (bridge issue). **Never depends on Zetta** — Zetta is closed; liquify shares only its
   *choices* (SDL3 + Dear ImGui) and the same open libraries.
3. **C++26**, CMake ≥ 3.28 + Ninja, Clang 17+ auto-selected the way liquid and Zetta do,
   GoogleTest. Dependencies via FetchContent; SDL linked static; one binary.
4. **Starting UI stack: SDL3 3.2 + Dear ImGui 1.91 with the SDL_Renderer backend**, the stack
   Zetta's headed mode proves on all three desktop platforms. The retained editor decision is
   recorded below.
5. **liquify is a client of libliquid.** liquid is **not** a submodule here: it is a sibling
   module of `liquid-bridge`, resolved by `LIQUIFY_LIQUID_SOURCE_DIR` (default `../liquid`),
   built library-only and linked as `liquid::liquid`. Everything liquify knows about
   repositories, drips, builds and search comes through that API; liquify never reimplements
   it. The pin lives in the bridge — bump it there, and move the `ref:` in
   `.github/workflows/ci.yml` to match.
6. **The editor buffer is a retained GPU text layer inside Dear ImGui** (Steve, 2026-09-05).
   SDL3 and ImGui remain the shell and panel toolkit; the buffer owns a glyph atlas and cached
   per-blob line layout. This records bridge decision 15 and closes liquify #2's question.
7. **AI is a first-class but optional editor surface and is off by default** (2026-09-05).
   Its visible repository switch is the network boundary: off constructs no provider and opens no
   socket. Context comes from editor, symbol, build and dependency graph facts, is byte-budgeted,
   and is shown before sending. Proposed edits require user acceptance and produce one user drip.
8. **The native plugin API extends liquid's frontend C boundary** (2026-09-06). Query kinds enter
   the in-process graph by name, commands carry a default binding and canonical-CBOR argument
   schema, and panels return data-only canonical-CBOR view models; plugins never call ImGui.

## Rules

- `src/app.cpp` is the **one** translation unit that includes SDL3 or ImGui. No other file
  may name an SDL or ImGui type; the shell exposes plain C++ (`liquify::App`).
- Read Zetta only from the umbrella bridge
  (`/Users/stevesperandeo/dev/homer6/orama-work/modules/zetta-bridge/modules/zetta`), and
  only to learn its patterns (`src/gui/app.cpp` for the SDL/ImGui lifecycle). Never copy
  Zetta source into this repository.
- The editor must not run ahead of the architecture: features that need the store, graph
  or daemon wait for liquid's Phases 1 and 4. Until then liquify is a shell.
- `--smoke` is the display-required self-test; keep it working, and keep `version` and the
  unit tests runnable without a display.
- `../liquid` is a shared checkout, not liquify's working copy: never commit liquid changes
  from inside liquify. Change liquid in its own repo, push, then bump the pin in
  `liquid-bridge` (and the CI `ref:` here).
- `src/ai_provider.cpp` is the only liquify network implementation. It uses the separately pinned
  Corosio and Boost.HTTP submodules on liquid's Capy runtime context. Do not add another network
  client or bypass the AI switch.
- Credentials come from the OS credential store or `ANTHROPIC_API_KEY`. Never persist, log, or
  display their value; UI state is only configured or not configured.

## Never

- Never depend on Zetta or on `oramasearch/lake`. liquify talks to liquid (the daemon), and
  through liquid to lake.
- Never make this repository public without the licence decision landed.

## Before finishing any change

1. **Document the change in `docs/<topic>/*.md`** (see `docs/README.md`). This is a must.
2. `README.md` is the front door only (what it is, build and run, pointers); never append status or
   feature sections to it. Update `docs/design/status.md` for the status of your area and
   `docs/README.md` for any new page.
3. Does this file's **Status**, **Decisions** and **Open decisions** still hold?
4. The full local proof is green: `cmake -S . -B build -G Ninja && cmake --build build &&
   ctest --test-dir build --output-on-failure && ./build/liquify version &&
   ./build/liquify --smoke 30`.
5. Workflow changes have green Linux, macOS and Windows jobs.
6. Commit means push.
