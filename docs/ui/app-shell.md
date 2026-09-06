# Native application shell

liquify uses the Orama design system as a native editor shell. The full window contains a narrow
activity bar, a contextual sidebar, a tabbed editor area, a tabbed bottom panel, and a status bar.
The editor tab can split into a primary retained text surface and a secondary overview. Every
divider is interactive.

## Layout and existing panels

The activity bar switches among Repository, Search, Review, Build, and Multiplayer. Repository
continues to render `RepositoryView`; Review continues to render `ReviewController`; Build invokes
the existing editor-graph build, test, and deploy requests. Search and Multiplayer hold their
places for their graph-backed leaves without inventing local data paths.

The bottom panel selects Diagnostics, Build output, Assistant, or Plugins. It hosts the existing
diagnostic/completion list, build/test/deploy event stream, default-off assistant, and data-only
plugin panels. Structural selection and edit-preview windows remain attached to the editor area.
Moving a panel changes only where its existing controller is drawn.

For an open repository, `.liquid/liquify-state.json` stores sidebar width and visibility, bottom
panel height and visibility, editor split ratio and visibility, active sidebar and panel, and the
theme preference. Invalid state reports an editor status message and uses bounded defaults.

## Design, fonts, and themes

`src/app.cpp` maps the ImGui style colors and metrics from
[`design_tokens.generated.hpp`](../../include/liquify/design_tokens.generated.hpp). Light and dark
palettes come from the upstream token modes. The preference cycles system → light → dark; system
mode reads `SDL_GetSystemTheme()` every frame, so an OS change is reflected at runtime.

IBM Plex Sans is loaded at 11, 12, 14, 16, 20, 24, and 32 logical pixels. IBM Plex Mono is loaded
at 12, 14, and 16 logical pixels and backs both retained editor geometry and the secondary split.
Dear ImGui's rasterizer density follows `SDL_GetWindowDisplayScale()` at startup and after a
display-scale event, keeping the logical scale while rebuilding sharper glyphs for dense screens.
The source and token gate are described in [`design-system.md`](design-system.md).

## macOS conventions

On macOS, CMake compiles `src/app.cpp` as Objective-C++ so it remains the only SDL/ImGui
translation unit while installing an AppKit menu bar. File, Edit, Selection, View, Go, Run, and
Review are native top-level groups. Cmd-S saves, Cmd-Shift-P opens the palette, Cmd-B toggles the
sidebar, Cmd-J toggles the bottom panel, Cmd-backslash toggles the editor split, and Cmd-Shift-T
cycles the theme. F7, F8, F9, F12, and F1 retain their existing build, test, deploy, definition,
and hover behavior.

The native window title is `<repository> — <current change> — liquify`; an open review replaces
`Working change` with its change identifier.

## Deterministic screenshots

Issue #38's general screenshot leaf has not landed, so this change supplies the smaller path
needed to review the shell. `--screenshot` selects one of nine seeded shell states, SDL's dummy
video driver and software renderer produce a hidden fixed 1440×900 frame, and the in-tree PNG
writer captures RGBA output. No display, repository, network, or container is involved.

```sh
./build/liquify --screenshot editor --theme dark --out editor-dark.png
./tools/ui/screenshot.sh ./build/liquify
```

The committed evidence is:

| State | Light | Dark |
|---|---|---|
| Welcome | [light](screenshots/welcome-light.png) | [dark](screenshots/welcome-dark.png) |
| Repository | [light](screenshots/repository-light.png) | [dark](screenshots/repository-dark.png) |
| Editor tabs and split | [light](screenshots/editor-light.png) | [dark](screenshots/editor-dark.png) |
| Diagnostics | [light](screenshots/diagnostics-light.png) | [dark](screenshots/diagnostics-dark.png) |
| Build/test/deploy | [light](screenshots/build-light.png) | [dark](screenshots/build-dark.png) |
| Assistant | [light](screenshots/assistant-light.png) | [dark](screenshots/assistant-dark.png) |
| Review | [light](screenshots/review-light.png) | [dark](screenshots/review-dark.png) |
| Plugins | [light](screenshots/plugins-light.png) | [dark](screenshots/plugins-dark.png) |
| Structural editing | [light](screenshots/structural-light.png) | [dark](screenshots/structural-dark.png) |

`Png.WritesRgbaPixelsWithTheRequestedDimensions` checks the encoder signature and dimensions;
`AppShellState` tests cover repository-local round trips and invalid state. The screenshot script
checks the complete matrix and each capture command reports its dimensions.
