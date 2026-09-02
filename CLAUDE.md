# CLAUDE.md — liquify

The IDE for liquid. **Org-wide conventions** live in
[`oramasearch/orama-bridge`](https://github.com/oramasearch/orama-bridge) `CLAUDE.md`
and apply in full. Project conventions live in
[`oramasearch/liquid`](https://github.com/oramasearch/liquid) `CLAUDE.md` — vocabulary,
document rules, confidentiality, and the decisions not to drift past. Read both.

## Status

**Hello world** (bridge `docs/action-plan.md`, Phase 0.5). `./build/liquify` opens a window
that says hello, `liquify version` prints, `liquify --smoke` renders 30 frames and exits 0,
`ctest` is green. Next: Phase 4, the editor surface on liquid's graph.

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

- **Editor buffer rendering.** Dear ImGui for chrome and panels is settled for now; the text
  buffer may need a retained, GPU-rendered text layer (Zed-style) inside it. Decide when the
  first real editing surface is built.
- **Plugin ABI.** A stable non-C++ boundary, shared with liquid's codecs and language
  frontends; not before Phase 4.

## Never

- Never depend on Zetta or on `oramasearch/lake`. liquify talks to liquid (the daemon), and
  through liquid to lake.
- Never make this repository public without the licence decision landed.

## Before finishing any change

1. Does `README.md` still describe what builds and runs? Update the status line and tables.
2. Does this file's **Status**, **Decisions** and **Open decisions** still hold?
3. `cmake --build build && ctest --test-dir build` green, and `./build/liquify --smoke` exits 0.
4. Commit means push.
