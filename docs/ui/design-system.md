# Orama design system

liquify compiles its native product-interface constants from the same pinned
`oramasearch/orama-design` DTCG snapshot as lake. The source is deterministic and offline:
`design/vendor/orama-design/` carries only the eight token JSON files and `UPSTREAM.json` records
the upstream commit plus a SHA-256 digest for each file.

## Token pipeline

`design/design-tokens.map.json` is the reviewed boundary between liquify names and upstream DTCG
paths. `design/design-tokens.inventory.json` fixes the public native role set. The dependency-free
`design/build-tokens.mjs` resolves aliases and light/dark values, then writes the committed
`include/liquify/design_tokens.generated.hpp` header. It contains normalized RGBA palettes,
spacing, control and shell sizes, radii, line heights, the full type scale, and embedded font
bytes. Do not edit the generated header or vendored JSON by hand.

Refresh from the pinned design-system checkout and regenerate with:

```sh
node design/sync-design-system.mjs \
  --from /path/to/orama-design
node design/build-tokens.mjs
```

The first command deliberately refreshes tokens only. Font updates are reviewed separately with
their source commit, licence and hashes in `design/fonts/UPSTREAM.json`.

## Fonts

IBM Plex Sans Regular and IBM Plex Mono Regular come from `IBM/plex` commit
`bf260093582f04622aacc1e9f9ca604d7ccd0c42`. Their OFL-1.1 licence and stamped hashes live beside
the TrueType files under `design/fonts/`. The generated header embeds them so packaged builds stay
one binary and never depend on a system font or network request.

The shell loads Sans at the design-system type sizes and Mono for editor text. Physical atlas
sizes follow SDL's per-window display scale so the logical type scale remains constant on
high-pixel-density displays.

## Gate

The gate verifies every stamped token and font digest, compares the committed header with a fresh
compile, and proves that changed generated output and a changed vendored token are rejected:

```sh
cmake --build build --target liquify-token-gate
```

CTest runs the same gate as `liquify-token-gate`. Build and deployment do not generate tokens;
Node.js participates only in configure-time test availability and the explicit gate.

## Table headers

Tables (the command palette's results, the front door's Recent repositories) use lake's compact
table header: the `surface` role behind it, labels in the `text_secondary` (muted ink) role, and a
`border_subtle` hairline under the row (`table_headers` in `src/app.cpp`; `TableHeaderBg`,
`TableBorderStrong`, `TableBorderLight` in `App::apply_theme`). The palette's rows alternate
between transparent and the `layer` role. The front door's Recent repositories is lake's compact
list instead: one-line rows separated by hairlines, 16-pixel cell padding, `layer_hover` on the
hovered row and the row's secondary action shown only while it is hovered. ImGui's default header, a near-black bar with dark labels, is never shown
(liquify#109).

`Screenshot.TableHeadersReadAtAAContrastInBothThemes` renders the palette and the front door in
light and dark, takes each header row's most common colour as its background and the pixel that
differs most as its ink, and fails below 4.5:1. The native scenario `table-headers` captures both
tables in the real window.

## Type scale

liquify sets text in five roles from the design tokens lake uses (`text24`, `text16`, the UI size,
`text12`; IBM Plex Sans Regular and SemiBold):

| Role | Face | Where |
|---|---|---|
| Eyebrow | Plex Sans Regular 12, upper case, tertiary ink | Above a page title: the front door's "THE IDE FOR LIQUID" |
| Title | Plex Sans SemiBold 24 (`App::type_title`) | The front door's "Welcome to liquify", the empty editor's "liquify" |
| Heading | Plex Sans SemiBold 16 (`App::type_heading`, `App::begin_dialog`) | Every dialog title, every panel header, the front door's "Recent Repositories" |
| Body | Plex Sans Regular at the UI size | Everything else; supporting copy under a title uses the secondary ink |
| Caption | Regular, disabled ink | Hints under fields and secondary facts |

Titles and headings are Title Case and buttons and labels sentence case, following lake's
*Capitalization* table: Quick Open, Command Palette, Symbol Search, Open Recent, New File, Move or
Rename Path, Review Comment, Rename Symbol, Extract Function, What Will Be Sent, Clone from Lake,
New Repository, Set Lake, Set Up Build, Cut Change, Request Review. The empty editor centres its
title, supporting copy and next step as one block.

`Screenshot.TypeScaleSeparatesTitlesHeadingsAndBody` measures rendered rows: the front door's
title is at least 1.5 times its copy, its section heading taller than the copy, and a panel header
taller than body text, in both themes. The native scenario `look` captures the empty editor, the
palette, Settings, the editor and the front door in the real window.

## Controls

liquify's controls mirror lake's kit (liquify#101), in `src/app.cpp`.

**Every button is `kit_button`.** No screen calls `ImGui::Button` or `ImGui::SmallButton` directly:
`SmallButton` drops the vertical padding (its label touched its border), and plain buttons took
ImGui's defaults and fixed widths. The two exceptions draw their own frame on purpose: the
activity bar's square icon buttons (lake's navigation look: no frame, `layer_hover` on hover,
`selected` with a 2-pixel `action` bar on the left for the current panel) and the click-to-copy
identifiers in details, which are compact quiet buttons outside `App`.

Sizes, from lake's control tokens:

| Size | Height | Horizontal padding | Where |
|---|---|---|---|
| `ButtonSize::regular` | `control_sm` (32) | `space3` (12) | Dialogs, the front door's recovery actions, the editor toolbar, panel actions |
| `ButtonSize::compact` | `space6` (24) | `space2` (8) | Inside table rows and inline lines: Recent repositories' Open in new window, the front door's Your identity and New window, review and assistant actions, details, breadcrumbs, the status bar |

The vertical padding is worked out from the font (`(height - font size) / 2`), so a label stays
centred at every UI size, and a button is as wide as its label plus its padding unless a width is
given. Kinds:

| Control | Helper | Look |
|---|---|---|
| Primary button | `kit_button(label, ButtonKind::primary)` | Filled with `action`, white label. One per surface: Save in the editor toolbar, the confirm button of each dialog (Clone, Create repository, Cut change, Set lake, Request review, Record and build, Post comment, Apply, Create, Store token, Add, Preview), Find in Search, Ask and Accept edit in the assistant |
| Secondary button | `kit_button(label, ButtonKind::secondary)` | lake's default button: `action` label on a `border_interactive` hairline, no fill; on hover it fills with `action_hover` and the label turns white |
| Quiet button | `ButtonKind::quiet` | No fill or border, `text_secondary` label; `layer_hover` on hover. The toolbar's view toggles and editing modes, breadcrumbs, the status bar, settings Reset, `...` menus |
| Toggle | `ButtonKind::toggle` | A mode that is on: `action_soft` fill, `border_interactive` hairline, `action` label. AI on in the toolbar (AI off is quiet) |
| Destructive button | `ButtonKind::destructive` | Filled with `danger`, white label |

Bottom panel tabs follow lake's tabs: the label centred on the 32-pixel row beside its icon,
`text_secondary` until selected, and the selected tab marked by a 2-pixel `action` underline
instead of a filled box. Settings section headers sit on the neutral `layer` role, keeping the
selected blue for selection.

| Control | Helper | Look |
|---|---|---|
| Checkbox | `checkbox(label, &value)` | A compact box with a `border_strong` outline; checked is filled with `action` and a white mark. Every checkbox has a label |
| Field | `text_field(label, &value, hint)` | Label above, full width of its panel, `surface` with a hairline |
| Slider | `slider_int(label, &value, min, max)` | Label above, full width, one handle shape whatever the range |

The editor toolbar is one row: Save, then Build, Tests and Push, then the view toggles and the
editing modes, divided by hairlines; each button has a tooltip. The keymap settings label each
binding by its command's title, not its id.

`Screenshot.SearchFieldsFillTheirPanel` checks the Search panel's fields share one width and fill
their panel; the native scenario `look` captures the toolbar, Search, Settings and the front door
in the real window.

## Action rows

A screen whose job is to choose a way in (the front door) shows its choices as **action rows**
rather than a row of equal buttons, as lake's entry pages do: one panel, each row the full width
and 64 pixels tall, a 32-pixel icon tile, a label and a one-line description in the secondary ink,
`layer_hover` on hover and a hairline between rows. The one-button rule still holds: the primary
choice's tile is filled with `action` and its label is in `action`; the others' tiles use
`action_soft`. Secondary actions sit under the panel as quiet compact buttons. Icons are the
native strokes of `draw_shell_icon`; the front door added `new_folder`, `clone`, `window`, `key`
and `branch`.

## Surfaces

Lake's direction is flat surfaces and hairline boundaries, with shadows only on overlays
(liquify#102). The shell's regions (activity bar, sidebar, editor, bottom panel, status bar) are
children with no border, no rounding and no gap: they share edges, and a `border_subtle` hairline
is drawn on each shared edge (`App::shell_region_begin`, `App::hairline`). The sidebar and bottom
panel resize from a splitter that straddles the hairline without taking space
(`App::edge_splitter`). The root window has no padding, so regions reach the window's edges.

- The status bar is one compact row: a body line with `space2` above and below.
- The editor gutter fills the editor's height, not only the lines that exist.
- File-kind icons come from one table (`file_icon`); a CMake file has its own build-file glyph
  instead of the plugin's plug.
- The modal scrim is the theme's canvas, translucent (`ModalWindowDimBg`), so a dialog dims dark
  mode instead of washing it out with mid-grey.

`Screenshot.ShellRegionsShareEdges` checks in both themes that neighbouring regions share an edge,
that the regions reach the window's edges, and the status bar's height. The native scenario `look`
captures the whole window and a dialog open in dark.
