# Layout properties and rendering proof

The native window regression and `Screenshot.PanelRendersDiagnostics` consume
`App::frame_layout()`. A layout edit changes the app's reported geometry; it does not require
new pixel constants in either test. This implements liquify issue #62.

## What the app reports

`UiFrameLayout` is a plain C++ record, replaced at the start of every frame. All rectangles are
in logical window coordinates. The record contains the root window extent, drawable scale,
active theme colors, toolbar and retained editor bounds, and panel content and tab bounds keyed
by `PanelId`. Hidden surfaces have no current content bounds. `diagnostics_viewport()` remains
available and reads the current Diagnostics entry, including floating and sidebar placement.

Toolbar text and diagnostic error ink come from the vertices submitted by their text draw calls.
The retained layer reports the union of fully visible glyph quads and its logical line height.
The reported ink bounds describe submitted geometry; they do not claim that the GPU displayed it.
The tests independently inspect framebuffer pixels in those reported areas.

When moving the toolbar, keep its measurement around the complete group. When adding a panel,
enter it through `draw_panel(id)`, which records the available content rectangle for every
placement. Record its tab rectangle and selected state at the tab control. To test meaningful
text, capture the vertex index immediately before a text-only draw and report `text_geometry`
immediately afterward. Backgrounds, borders and icons must not be included in text ink. A panel
fixture should contain a known message, and its test must require that message's reported ink
inside the panel content. Resetting the frame record prevents yesterday's visible panel from
satisfying today's hidden-panel assertion.

## Properties and their limits

The shared checks live in `tests/ui_property_assert.hpp`:

- The reported window and drawable dimensions agree at the reported scale. More than half of
  the framebuffer must differ from the clear color. Nothing drawn reports **shell is blank**;
  half-scale geometry can occupy at most a quarter of the drawable and reports **geometry does
  not fill the window**.
- Text is measured as contrast against the reported surface fill, rather than identity with one
  foreground color. This allows syntax roles and antialiasing. It must have a plausible ink
  fraction, enough distinct pixel rows for the reported logical line height and drawable scale,
  and horizontal extent across the reported text bounds. Failure output reports each measured
  value and its floor.
- Toolbar bounds must be non-empty and inside the window. Its label must satisfy the shared text
  distribution rule.
- The editor must occupy its reported area: at least 40% retains the surface color, and its
  submitted source region must satisfy the shared text distribution rule. This replaces the
  former right-side dark-pixel count and checks horizontal and vertical reach.
- The visible Diagnostics panel must occupy its reported content area. Its active tab must
  report selection and show the selected color over at least 25% of the tab area. The screenshot
  case also requires the diagnostic error ink inside that panel, in both light and dark themes.
- The native case verifies atlas rasterizer density against drawable scale for shell and editor
  fonts. It repeats properties after resize, font rebuild and display moves, with a density-2
  window and a density-1 control on macOS. Both hidden and visible bottom-panel states run.
- Logical pointer hit testing and exact palette/floating-panel occlusion remain. The prior popup
  check sampled the clipped rectangle, which could include pixels outside the opaque client area.
  It now uses the reported client area after its current padding; the floating check uses reported
  content. Neither needs an inset constant. Floating placement in the test derives from the editor
  area.

Ratios tolerate font filtering and decoration without prescribing positions. The surface occupancy
check assumes a flat surface fill; if the shell gains a gradient or texture, it needs a reported
fill contract before that check changes. Text uses surface contrast, so it allows syntax roles and
edge antialiasing. These are rendering and legibility proxies,
not OCR or an accessibility conformance claim. The half-window coverage rule assumes the normal
workspace fills most of the window; an intentionally sparse new window needs its own explicitly
named contract. Existing unrelated screenshot cases still cover their own fixture landmarks.
No new fixed sample rectangle is needed for these checks.

## Repeatable demonstration

With a configured native build, run:

```sh
python3 tools/ui/check-properties.py --build build
```

The script runs the **same two tests** four ways, then restores and rebuilds the source:

1. Baseline: both tests pass.
2. Blank shell: omit the renderer submission after clearing; both tests must fail with the blank
   shell diagnosis.
3. Quarter-size geometry: halve the renderer scale on each axis; both tests must fail with the
   window coverage diagnosis.
4. Moved toolbar: move the toolbar groups down by one current frame-height; both tests must pass
   with unchanged assertions. The editor consumes the remaining space.

Mutations are temporary source edits, not production switches. The script checks that each
mutation target occurs once and restores the original source bytes in `finally`, including after
failure. It builds with at most six jobs and runs one suite at a time. Run it with exclusive
access to this worktree. Build logs, test output and native readbacks stay under the selected
build directory; `property-proof/` retains each run's output.

The full acceptance additionally requires the native static and shared suites and opening the
built `liquify` application on the real desktop. Headless success alone is insufficient. See
[the native rendering contract](app-shell.md#native-window-regression) and
[local proof](../build/local.md).
