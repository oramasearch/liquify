# Native application shell

liquify uses the Orama design system as a native editor shell. The full window contains a narrow
activity bar, a contextual sidebar, a tabbed editor area, a tabbed bottom panel, and a status bar.
The editor tab can split into a primary retained text surface and a secondary overview. Every
divider is interactive.

## Layout and registered panels

The activity bar switches among Repository, Search, Review, Build, and Multiplayer. Repository
continues to render `RepositoryView`; Review continues to render `ReviewController`; Build invokes
the existing editor-graph build, test, and deploy requests. Search runs liquid repository queries
and opens results at their source locations. Multiplayer displays the presence records used by the
editor gutter.

The bottom area hosts Diagnostics, Build output, Assistant, or Plugins. Search inputs and results
share the registered Search panel, which defaults to the sidebar and can move into the bottom area.
Structural selections and edit previews open in their registered editor tab.
Moving a panel changes only where its existing controller is drawn. Every surface now enters the
plain-C++ registry described in [`panels.md`](panels.md). Its panel menu moves surfaces among all
three dock areas, floats and re-docks them, while one status channel presents build, saved-drip
sync, repository hydration, and assistant progress.

For an open repository, `.liquid/liquify-state.json` stores sidebar width and visibility, bottom
panel height and visibility, editor split ratio and visibility, panel placements, visibility,
order, focus, floating bounds and prior dock, plus the theme preference. Version 1 selection
fields migrate into version 2 panel state. Invalid state reports an editor status message and uses
bounded defaults. Reset layout restores the shell and panel defaults without changing the theme
preference.

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
translation unit while installing an AppKit menu bar. The menu bar is generated from the same
top-level registry groups, active bindings, and plugin commands shown by the palette. Cmd-S saves,
Cmd-Shift-P opens the palette, Cmd-B toggles the
sidebar, Cmd-J toggles the bottom panel, Cmd-backslash toggles the editor split, and Cmd-Shift-T
cycles the theme. F7, F8, F9, F12, and F1 retain their existing build, test, deploy, definition,
and hover behavior.

The native window title is `<repository> — <current change> — liquify`; an open review replaces
`Working change` with its change identifier.

## Deterministic screenshots

The named screenshot registry in [`README.md`](README.md) selects twelve seeded shell states in
light and dark. SDL's dummy video driver and software renderer produce a hidden fixed 1280×800
frame, and the in-tree PNG writer captures RGBA output. No display, repository, network, or
container is involved.
The structural capture uses a fixed edit-preview model, avoiding frame-timed caret state while the
live rename and extract-function modals keep their normal input behavior.

```sh
./build/liquify --screenshot editor-cpp-dark --out editor-dark.png
LIQUIFY_BINARY=./build/liquify ./tools/ui/screenshot.sh
```

The committed evidence is:

| State | Light | Dark |
|---|---|---|
| Welcome | [light](screenshots/welcome-light.png) | [dark](screenshots/welcome-dark.png) |
| Repository | [light](screenshots/repository-light.png) | [dark](screenshots/repository-dark.png) |
| Editor tabs and split | [light](screenshots/editor-cpp-light.png) | [dark](screenshots/editor-cpp-dark.png) |
| Editor view | [light](screenshots/editor-view-light.png) | [dark](screenshots/editor-view-dark.png) |
| Diagnostics | [light](screenshots/diagnostics-light.png) | [dark](screenshots/diagnostics-dark.png) |
| Build/test/deploy | [light](screenshots/build-light.png) | [dark](screenshots/build-dark.png) |
| Assistant | [light](screenshots/assistant-light.png) | [dark](screenshots/assistant-dark.png) |
| Review | [light](screenshots/review-light.png) | [dark](screenshots/review-dark.png) |
| Plugins | [light](screenshots/plugins-light.png) | [dark](screenshots/plugins-dark.png) |
| Command palette | [light](screenshots/command-palette-light.png) | [dark](screenshots/command-palette-dark.png) |
| Structural editing | [light](screenshots/structural-editing-light.png) | [dark](screenshots/structural-editing-dark.png) |
| Panel system | [light](screenshots/panels-light.png) | [dark](screenshots/panels-dark.png) |

`Screenshot.EditorRendersSelectedSource`, `Screenshot.PanelRendersDiagnostics`, and
`Screenshot.EditorViewShowsSearchGutterAndBothThemes`,
`Screenshot.CommandPaletteShowsSearchAndRecentCommands`, and
`Screenshot.RegistryIncludesPanelSystemInBothThemes` read the
generated PNG files and check stable shell pixels; `PanelRegistry` and `AppShellState` tests cover
registry behavior, repository-local round trips, version 1 migration, reset, and invalid state. The
screenshot script checks the complete matrix and each capture command reports its dimensions.
