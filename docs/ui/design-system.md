# Orama design system

liquify compiles its native product-interface constants from a pinned DTCG snapshot of Orama's
design system, the same snapshot lake uses. The tokens are OramaSearch, Inc.'s own work and the
vendored copy is released under the MIT License with the rest of this repository (`COPYRIGHT`). The source is deterministic and offline:
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

The font atlas is built for Basic Latin, Latin-1 and General Punctuation (U+2010 to U+205E), so
the em dash, the ellipsis, curly quotes and the bullet in liquify's own text draw from IBM Plex
instead of "?" (liquify#129). Characters outside those ranges, such as CJK composition, come from
the input method's own preview.

## Gate

The gate verifies every stamped token and font digest, compares the committed header with a fresh
compile, and proves that changed generated output and a changed vendored token are rejected:

```sh
cmake --build build --target liquify-token-gate
```

CTest runs the same gate as `liquify-token-gate`. Build and deployment do not generate tokens;
Node.js participates only in configure-time test availability and the explicit gate.

## Table headers

Tables (the front door's Recent repositories) use lake's compact table header: the `surface` role behind it, labels in the `text_secondary` (muted ink) role, and a
`border_subtle` hairline under the row (`table_headers` in `src/app.cpp`; `TableHeaderBg`,
`TableBorderStrong`, `TableBorderLight` in `App::apply_theme`). The palette's rows alternate
between transparent and the `layer` role. The front door's Recent repositories is lake's compact
list instead: one-line rows separated by hairlines, 16-pixel cell padding, `layer_hover` on the
hovered row and the row's secondary action shown only while it is hovered. ImGui's default header, a near-black bar with dark labels, is never shown
(liquify#109).

`Screenshot.TableHeadersReadAtAAContrastInBothThemes` renders the palette's list heading and the
front door's table header in light and dark, takes each header row's most common colour as its background and the pixel that
differs most as its ink, and fails below 4.5:1. The native scenario `table-headers` captures both in
the real window.

## Command palette

The command palette is lake's Go to (liquify#34): a titled dialog (`Command Palette`), a full-width
search field (`palette/search`), a list heading in `text_secondary` with the result count and a
`border_subtle` hairline under it (`palette/header`), then one compact row per action
(`palette/row/<command>`). A row is the title in `text_primary`, the group after it in
`text_secondary`, and the active binding as a key chip on the right: IBM Plex Mono 12 in
`text_secondary` on `surface`, with a `border_subtle` outline and the panel radius. A hovered row
takes `layer_hover`; the selected row takes `selected`. The first result is selected, Up and Down
move the selection (and hovering a row moves it there), Enter runs it, a click runs the clicked row
and Escape closes the dialog. Rows are not keyboard navigation targets, so the search field keeps
the keyboard. A muted footer names the keys. `App::draw_command_palette` in `src/app.cpp` draws it.

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

## Editor chrome

The editor area follows lake's quiet chrome (liquify#127). The region has no window padding of
its own: its rows keep `space3` inside, and the text surface reaches the region's edges.

- **One row carries the file**, `control_sm` (32) tall with a `border_subtle` hairline under it.
  With only the file open, the breadcrumb is the row: path segments and the enclosing symbol as
  compact quiet buttons divided by a disabled `/`, an unsaved `*`, and the `...` file menu at the
  row's right edge. When panels open as editor tabs, the row starts with lake's tabs (the file's
  name, then each panel with its icon): `text_secondary` until selected, the selected tab marked by
  a 2-pixel `action` underline, never a filled box. The breadcrumb then follows the tabs without
  the file's own name, which its tab already shows. Right-click a panel tab to close or move it.
- **Left edges line up**: the first label's ink in the file row, the toolbar's Save button and the
  lines under the toolbar (find, lease and hover text) start `space3` in from the region.
- **The toolbar** sits `space2` under the file row; one hairline closes the chrome `space2` under its
  last line, and the text surface starts under it.
- **The gutter has no block fill**: it is the editor's `surface`, divided from the text by a
  `border_subtle` hairline over the editor's full height. Line numbers are glyphs in the editor's
  own face, right-aligned `space4` before the hairline (the fold column), in `text_tertiary`; the
  current line's number is `text_secondary`.
- **Focus is the caret and the current line**: no rule is drawn across the editor. While the editor
  has focus the caret's line has a `layer` band across the text. The caret is `text_primary`, a
  selection the `selection` role, in both themes. A file dragged over the editor outlines it with a
  1-pixel `focus` line.

## Notices

A failure, a warning or a piece of information shown in place is lake's notice
(`App::draw_user_error`, [`errors.md`](errors.md) has the full rules):

| Part | Token |
|---|---|
| Fill | `danger_soft`, `warning_soft` or `info_soft` |
| Hairline | the role (`danger`, `warning`, `info`) at half strength, 1 pixel, `panel_radius` corners |
| Status dot | the role, 6 pixels |
| Padding | `space2` (8) above and below, `space3` (12) at the sides; `space2` for a quiet notice |
| Sentence | body, `text_primary` |
| Next step | body, `text_secondary` |
| Action | compact secondary button (the one-button rule: never a second primary on the surface) |
| Details | compact quiet button |

It is one line when the panel has room. A quiet notice is always one line and keeps its next step
in a tooltip; the repository sidebar uses it for failures that leave the tree working.

Loading and empty states are one quiet line of body text in `text_secondary`
(`App::draw_panel_state`), not the disabled ink, which is for hints under fields.

## Surfaces

Lake's direction is flat surfaces and hairline boundaries, with shadows only on overlays
(liquify#102). The shell's regions (activity bar, sidebar, editor, bottom panel, status bar) are
children with no border, no rounding and no gap: they share edges, and a `border_subtle` hairline
is drawn on each shared edge (`App::shell_region_begin`, `App::hairline`). The sidebar and bottom
panel resize from a splitter that straddles the hairline without taking space
(`App::edge_splitter`). The root window has no padding, so regions reach the window's edges.

- The status bar is one compact row: a body line with `space2` above and below.
- The editor gutter's hairline runs the editor's height, not only the lines that exist (*Editor
  chrome* above).
- File-kind icons come from one table (`file_icon`); a CMake file has its own build-file glyph
  instead of the plugin's plug.
- The modal scrim is the theme's canvas, translucent (`ModalWindowDimBg`), so a dialog dims dark
  mode instead of washing it out with mid-grey.

`Screenshot.ShellRegionsShareEdges` checks in both themes that neighbouring regions share an edge,
that the regions reach the window's edges, and the status bar's height. The native scenario `look`
captures the whole window and a dialog open in dark.

The status bar is one row of lake's 32-pixel control height that never scrolls: its window
padding centres a 24-pixel line, its table has no vertical cell padding, scrolling is off for the
region, and each column's text is aligned to the compact buttons' frame (liquify#130).
