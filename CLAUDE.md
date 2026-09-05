# CLAUDE.md — liquify

The IDE for liquid. **Org-wide conventions** live in
[`oramasearch/orama-bridge`](https://github.com/oramasearch/orama-bridge) `CLAUDE.md`
and apply in full. Project conventions live in
[`oramasearch/liquid`](https://github.com/oramasearch/liquid) `CLAUDE.md` — vocabulary,
document rules, confidentiality, and the decisions not to drift past. Read both.

## Status

**Retained GPU editor with graph-backed interactive C/C++ analysis, build, test and deploy.**
`./build/liquify <path>` opens a liquid repository off the render thread, shows its root entries,
loads nested trees when expanded, and opens blobs into an editable in-memory buffer; save calls
libliquid and produces a drip.
The standard input mode is non-modal and a modal command/insert mode is available. Unreadable
repositories retain liquid's error, and `liquify --smoke` scrolls and edits a 50,000-line buffer,
reports frame and typing latency, and exits 0. liquid is pinned at `modules/liquid` through its
persisted demand-driven query graph, scheduler and worker fleet, authorized sync reference
updates with per-reference outcomes, repository search, continuous clean-rebuild validation,
configured repository build plans, daemon build-event streams and test actions. C and C++ stubs are
graph query values that resolve
declarations from closed files without retaining full parse trees. Graph-backed editor queries run
over error-tolerant incremental trees for open overlays, resolve through closed-file
stubs or compiler symbol records, and read build diagnostics by translation unit. The editor
surface renders those diagnostics inline and in a panel, exposes definition, hover and completion
demands, reports daemon drip triggers and the validation backlog, consumes daemon build events,
runs tests as graph actions, and displays sync reference outcomes for deploy (liquify #6).

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
   Zetta's headed mode proves on all three desktop platforms. Whether an immediate-mode UI is
   the right long-term *editor buffer* is the first real liquify decision (see Open decisions).
5. **liquify is a client of libliquid.** liquid is a submodule at `modules/liquid`, built
   library-only and linked as `liquid::liquid`. Everything liquify knows about repositories,
   drips, builds and search comes through that API; liquify never reimplements it. Bump the pin
   deliberately (one commit per bump, naming the liquid change it takes).
6. **The editor buffer is a retained GPU text layer inside Dear ImGui** (Steve, 2026-09-05).
   SDL3 and ImGui remain the shell and panel toolkit; the buffer owns a glyph atlas and cached
   per-blob line layout. This records bridge decision 15 and closes liquify #2's question.

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
- `modules/liquid` is a pin, not a working copy: never commit liquid changes from inside
  liquify. Change liquid in its own repo, push, then bump the pin here.

## Open decisions

- **Plugin ABI.** A stable non-C++ boundary, shared with liquid's codecs and language
  frontends; not before Phase 4.

## Never

- Never depend on Zetta or on `oramasearch/lake`. liquify talks to liquid (the daemon), and
  through liquid to lake.
- Never make this repository public without the licence decision landed.

## Before finishing any change

1. **Document the change in `docs/<topic>/*.md`** (see `docs/README.md`). This is a must.
2. Does `README.md` still describe what builds and runs? Update the status line and tables.
3. Does this file's **Status**, **Decisions** and **Open decisions** still hold?
4. The full local proof is green: `cmake -S . -B build -G Ninja && cmake --build build &&
   ctest --test-dir build --output-on-failure && ./build/liquify version &&
   ./build/liquify --smoke 30`.
5. Workflow changes have green Linux, macOS and Windows jobs.
6. Commit means push.
