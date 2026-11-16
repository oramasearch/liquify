# Native application shell

liquify uses the Orama design system as a native editor shell. The full window contains a narrow
activity bar, a contextual sidebar, a tabbed editor area, a tabbed bottom panel, and a status bar.
The editor tab can split into a primary retained text surface and a secondary overview. Every
divider is interactive.

## Layout and registered panels

The activity bar switches among Repository, Search, Review, Build, and Multiplayer. Repository
renders the annotated `RepositoryView` tree and Search filters its libliquid symbol records;
Review continues to render `ReviewController`; Build invokes the existing editor-graph build,
test, and push requests. Search also runs liquid repository queries and opens results at their
source locations. Multiplayer displays the presence records used by the editor gutter. Quick
Open, Symbol Search, recent locations, references, and call hierarchy use modal or result windows
over the same shell ([`navigation.md`](navigation.md)).

The bottom area hosts Diagnostics, Build output, Assistant, or Plugins. Search inputs and results
share the registered Search panel, which defaults to the sidebar and can move into the bottom area.
Structural selections and edit previews open in their registered editor tab.
Moving a panel changes only where its existing controller is drawn. Every surface now enters the
plain-C++ registry described in [`panels.md`](panels.md). Its panel menu moves surfaces among all
three dock areas, floats and re-docks them, while one status channel presents build, saved-drip
sync, repository hydration, and assistant progress.

For an open repository, `.liquid/liquify-state.json` stores sidebar width and visibility, bottom
panel height and visibility, editor split ratio and visibility, panel placements, visibility,
order, focus, floating bounds and prior dock, plus a compatibility copy of the theme preference and
the high-contrast preference. The `.liquid` settings files described in
[`settings.md`](settings.md) are authoritative for the theme. Version 1 selection fields migrate
into version 2 panel state; version 3 adds high contrast. Invalid state reports an editor status
message and uses bounded defaults. Reset layout restores the shell and panel defaults without
changing either color preference.

## Design, fonts, and themes

`src/app.cpp` maps the ImGui style colors and metrics from
[`design_tokens.generated.hpp`](../../include/liquify/design_tokens.generated.hpp). Light and dark
palettes come from the upstream token modes. The preference cycles system → light → dark; system
mode reads `SDL_GetSystemTheme()` every frame, so an OS change is reflected at runtime. The
searchable settings surface applies theme, supported font sizes, editor behavior, command bindings,
assistant configuration, lake accounts, and telemetry without reopening the repository. A separate
high-contrast preference increases text, border, selection and focus contrast, and macOS Increase
Contrast enables it automatically. macOS Reduce Motion removes hover transition delay.

IBM Plex Sans is loaded at 11, 12, 14, 16, 20, 24, and 32 logical pixels. IBM Plex Mono is loaded
at 12, 14, and 16 logical pixels and backs both retained editor geometry and the secondary split.
Font rasterizer density follows the window's pixel density at the frame boundary. Moving the
window or changing font settings rebuilds the atlas and invalidates every retained glyph's UV
coordinates, including when the logical font size is unchanged.
The source and token gate are described in [`design-system.md`](design-system.md).

## Logical coordinates and native pixels

Shell layout, retained editor vertices, glyph advances, clipping rectangles, pointer hit testing,
drop targets and composition rectangles all use window logical coordinates. `App::frame` sets
SDL's renderer scale once from ImGui's drawable-to-window ratio after the platform backend has
updated it; that scale remains active through shell drawing, retained drawing and presentation.
The pinned renderer backend detects this scale and passes logical clipping rectangles to SDL,
which scales both geometry and clipping exactly once. No editor-local scale restoration occurs.
A density-2 window therefore has twice as many pixels on each axis while retaining the same
logical controls and click positions; density 1 uses the identical contract.

Retained editor drawing is an ImGui draw callback in the editor window's command list. Its clip
is the intersection of the editor rectangle and the enclosing ImGui clip. The following reset
callback restores renderer state for subsequent ImGui commands. Composition, tooltips, modal
backdrops, modal contents and floating panels consequently retain ImGui's normal paint order;
source text and the gutter cannot be painted over a later window.

Atlas creation, upload, filtering and blend configuration are checked at startup and after every
rebuild. A failure stops rendering and reports the renderer, atlas dimensions and SDL error in
stderr and a native error dialog, with instructions to reduce font sizes or restart the app.
The pinned backend does not check its upload result, so liquify performs a checked upload before
allowing a frame to use the texture.

## Native window regression

`liquify-native-window` is a separate CTest executable enabled by `LIQUIFY_NATIVE_WINDOW_TESTS`
(default on macOS). It requires a real desktop and GPU renderer; a missing display, dummy driver
or software renderer fails rather than substituting headless evidence. The existing screenshot
matrix remains a set of visual unit tests.

```sh
ctest --test-dir build -R '^liquify-native-window$' --output-on-failure
```

The test starts an ordinary visible `App` window and uses the deterministic screenshot suite's
in-memory `src/main.cpp` fixture. It deliberately avoids the current liquid CLI repository path
because its store-format compatibility is tracked in liquify#54; repository loading is outside
this rendering regression. It reads the native GPU framebuffer immediately before presenting it,
checks shell controls, source glyphs at reported extremes and panel occupancy, resizes the window, rebuilds fonts, moves
among attached displays and resolves logical pointer coordinates through the displayed text layout.
It compares the palette and a floating panel interior with and without source underneath:
every channel must match. Evidence PNGs are written to `build/tests/native-window/`. On macOS the
default high-density window must report scale 2; a separate native density-1 control window
exercises the same frame code without changing the production high-DPI default. Run this case on
a Retina desktop; `version` and the remaining unit tests remain display-independent.

The [pre-fix](screenshots/native-window-retina-before.png) and [fixed](screenshots/native-window-retina-after.png)
Retina captures are native GPU framebuffer readbacks, not `--screenshot` output. The fixed capture
shows the shell and `src/main.cpp` across the full drawable.

## Default-window hierarchy

The normal editor window reserves its reading path for content, direct controls, and state a
person can act on. The toolbar labels its File, Build, View, and Mode groups. Navigation and panel
tabs use drawn type or panel icons with text tooltips, and a panel title and count appear once in
its tab. The retained editor consumes the rest of its pane rather than drawing a second empty
focus frame around short files.

Attribution is a line-gutter marker: hover or click it to inspect and copy the full author,
session, and change values. The Multiplayer panel holds collaboration records; the File actions
menu holds lease operations. Diagnostics has an explicit Repository details control for drip,
validation, timing, and identity records. Identifiers in customer-facing details retain their
scheme and abbreviate their payload to eight characters until copied. The status bar uses a
table-sized row and a Details menu so its controls remain visible at narrow heights.

Native evidence for this change is [before](screenshots/issue-61-before.png) and
[after](screenshots/issue-61-after.png). The toolbar and diagnostics assertions read the bounds
the shell reports after layout, rather than assuming fixed pixel positions. The full rationale,
relocation record, and live-window acceptance are in
[`what-the-window-shows.md`](what-the-window-shows.md).

## macOS conventions

On macOS, CMake compiles `src/app.cpp` as Objective-C++ so it remains the only SDL/ImGui
translation unit while installing an AppKit menu bar. File, Edit, Selection, View, Go, Run, and
Review are native top-level groups. The command registry supplies the fuzzy palette, resolved
platform bindings, recent-command ordering, typed `.liquid` overrides, and plugin commands;
[`commands.md`](commands.md) is its generated reference. Cmd-S saves, Cmd-Shift-P opens the
palette, Cmd-B toggles the sidebar, Cmd-J toggles the bottom panel, Cmd-backslash toggles the
editor split, Cmd-Shift-T cycles the theme, Cmd-P opens paths, Cmd-T opens symbols, and Cmd-R
opens recent locations.

The native window title is `<repository> — <current change> — liquify`; an open review replaces
`Working change` with its change identifier.

## Deterministic screenshots

The named screenshot registry in [`README.md`](README.md) selects seventeen seeded shell states in
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
| Unreadable repository | [light](screenshots/repository-unreadable-light.png) | [dark](screenshots/repository-unreadable-dark.png) |
| Lake offline | [light](screenshots/lake-offline-light.png) | [dark](screenshots/lake-offline-dark.png) |
| Repository | [light](screenshots/repository-light.png) | [dark](screenshots/repository-dark.png) |
| Editor tabs and split | [light](screenshots/editor-cpp-light.png) | [dark](screenshots/editor-cpp-dark.png) |
| Editor view | [light](screenshots/editor-view-light.png) | [dark](screenshots/editor-view-dark.png) |
| Diagnostics | [light](screenshots/diagnostics-light.png) | [dark](screenshots/diagnostics-dark.png) |
| Build/test/push | [light](screenshots/build-light.png) | [dark](screenshots/build-dark.png) |
| Assistant | [light](screenshots/assistant-light.png) | [dark](screenshots/assistant-dark.png) |
| Review | [light](screenshots/review-light.png) | [dark](screenshots/review-dark.png) |
| Plugins | [light](screenshots/plugins-light.png) | [dark](screenshots/plugins-dark.png) |
| Structural editing | [light](screenshots/structural-editing-light.png) | [dark](screenshots/structural-editing-dark.png) |
| Panel system | [light](screenshots/panels-light.png) | [dark](screenshots/panels-dark.png) |
| Navigation | [light](screenshots/navigation-light.png) | [dark](screenshots/navigation-dark.png) |
| Settings | [light](screenshots/settings-light.png) | [dark](screenshots/settings-dark.png) |
| Accessibility | [light](screenshots/accessibility-light.png) | [dark](screenshots/accessibility-dark.png) |

`Screenshot.EditorRendersSelectedSource`, `Screenshot.EditorViewShowsSearchGutterAndBothThemes`,
`Screenshot.PanelRendersDiagnostics`, `Screenshot.NavigationRendersQuickOpenAndAnnotatedTree`,
`Screenshot.SettingsRenderGroupedControlsInBothThemes`, and
`Screenshot.AccessibilityStateShowsHighContrastFocusInBothThemes` read the generated PNG files and check
stable shell pixels; `PanelRegistry` and `AppShellState` tests cover registry behavior,
repository-local round trips, version 1 migration, reset, and invalid state. The screenshot script
checks the complete matrix and each capture command reports its dimensions.

The native and Diagnostics screenshot tests share [layout properties](property-tests.md):
window coverage, glyph extent at drawable scale, editor and panel occupancy, and active tab
visibility. The same page explains the required frame report and renderer mutation proof.

## Icons beside labels

Widgets that own their hit area — `Selectable`, `TreeNode`, `TabItem` — take a single label, so the
room for an icon has to live inside that string. `icon_label()` builds that prefix from the font's
own space advance and the caller's text offset, rather than a fixed number of spaces:

| Widget | Where its label starts |
|---|---|
| `Selectable` | the item's left edge — it applies frame padding **vertically only** |
| `TreeNode` | `ImGui::GetTreeNodeToLabelSpacing()` |
| `TabItem` | `ImGui::GetStyle().FramePadding.x` |
| `TextUnformatted` | the item's left edge |

Pass that offset as `icon_label`'s second argument and the icon keeps its gap at any font size or
display scale. A fixed count of spaces does not: six spaces cleared a 16 px icon at one size and
overlapped the first letter at another, which is how icons came to sit on top of their labels.

`Screenshot.IconsDoNotTouchTheirLabels` renders the repository panel, finds rows whose first ink run
is icon-shaped, and requires a gap before the label. It finds the rows by their ink rather than by
coordinates, so a layout change does not break it.

Toolbar group captions (`File`, `Build`, `View`, `Mode`) are indented by the frame padding so they
align with the text of the buttons beneath them, not with the button frames.
