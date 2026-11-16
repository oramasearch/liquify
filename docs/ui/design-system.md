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
`TableBorderStrong`, `TableBorderLight` in `App::apply_theme`). Rows alternate between transparent
and the `layer` role. ImGui's default header, a near-black bar with dark labels, is never shown
(liquify#109).

`Screenshot.TableHeadersReadAtAAContrastInBothThemes` renders the palette and the front door in
light and dark, takes each header row's most common colour as its background and the pixel that
differs most as its ink, and fails below 4.5:1. The native scenario `table-headers` captures both
tables in the real window.

## Type scale

liquify sets text in four roles from the design tokens lake uses (`text24`, `text16`, the UI size,
`text12`; IBM Plex Sans Regular and SemiBold):

| Role | Face | Where |
|---|---|---|
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

liquify's controls mirror lake's kit (liquify#101), in `src/app.cpp`:

| Control | Helper | Look |
|---|---|---|
| Primary button | `kit_button(label, ButtonKind::primary)` | Filled with `action`, white label. One per surface: Open folder on the front door, Save in the editor toolbar, the confirm button of each dialog (Clone, Create repository, Cut change, Set lake, Request review, Record and build), Find in Search |
| Secondary button | `kit_button(label, ButtonKind::secondary)`, and every plain `ImGui::Button` | `surface` with a `border_strong` hairline |
| Quiet button | `ButtonKind::quiet` | No fill or border until hovered: the toolbar's view toggles |
| Destructive button | `ButtonKind::destructive` | `danger` label and hairline |
| Checkbox | `checkbox(label, &value)` | A compact box with a `border_strong` outline; checked is filled with `action` and a white mark. Every checkbox has a label |
| Field | `text_field(label, &value, hint)` | Label above, full width of its panel, `surface` with a hairline |
| Slider | `slider_int(label, &value, min, max)` | Label above, full width, one handle shape whatever the range |

The editor toolbar is one row: Save, then Build, Tests and Push, then the view toggles and the
editing modes, divided by hairlines; each button has a tooltip. The keymap settings label each
binding by its command's title, not its id.

`Screenshot.SearchFieldsFillTheirPanel` checks the Search panel's fields share one width and fill
their panel; the native scenario `look` captures the toolbar, Search, Settings and the front door
in the real window.
