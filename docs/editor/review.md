# Review in place

## What exists

Start liquify with a lake repository URL and the matching local liquid repository:

```sh
LIQUID_TOKEN=<token> ./build/liquify --lake https://host/v1/repositories/<id> <path>
```

The Reviews section lists the lake reviews returned by `liquid::forge_client`. Opening a review
pulls the repository through `liquid::sync_endpoint`, takes the review change's first parent as the
base, and compares that base with the pulled head through `repository::diff(..., {.text = true})`.
The retained text layer shows an inline, read-only text diff: blue file and hunk headers, red
removed lines, green added lines, and orange conflict values. The header says `text diff` because
structural diff remains liquid #68.

`LIQUID_TOKEN` is passed only to libliquid's forge and sync clients. liquify does not implement a
lake HTTP client, persist the token, place it in a URL, or display it. The UI thread submits work
to `ReviewController`; liquid's serial runtime queue awaits every forge and sync coroutine and
publishes a copied snapshot for rendering.

## Cursor comments and review state

New Comment creates a one-based `text_anchor {change, path, line, column}` at the current buffer
cursor. In the inline diff, only current-side lines can receive a comment; removed lines have no
position in the pulled head. Purple gutter marks show comments on the current file or current-side
diff line. The Reviews section retains the comment bodies as their threads and labels every anchor:

| Label | Display |
|---|---|
| `exact` | The original position still identifies the same text. |
| `moved` | The exact text moved or shifted; the gutter mark follows the mapped line. |
| `approximate` | The best surviving nearby position, explicitly marked approximate. |
| `deleted` | No current cursor exists; the thread remains parked in the Reviews section with its original text. |

The controller polls the selected review. When sync exposes a different head for the same review,
it calls `forge_client::advance_anchors`, which advances lake's stored anchors through
`repository::map_anchors`, reloads the comments, and rebuilds the diff. Approve sets lake's review
state to `approved`. Lake has no separate requested-changes state, so Request Changes returns the
review to `open`; the review comments carry the requested work.

## Keyboard commands

All review commands are also in the command palette (primary-modifier+Shift+P):

| Command | Binding |
|---|---|
| Next hunk | Alt+Down |
| Previous hunk | Alt+Up |
| Next comment | Alt+C |
| New comment at cursor | Primary-modifier+Alt+C |
| Approve | Primary-modifier+Alt+A |

## Verify and change

`tests/test_review.cpp` uses libliquid's forge test double. It pulls and renders a change, posts a
cursor comment, advances a fixture head so two anchors shift and one line is deleted, verifies the
mapped states and positions, and exercises approve plus Request Changes. `liquify-review-bench`
measures the 50-file and 200-anchor cases recorded in [`README.md`](README.md).

Keep review transport inside `liquid::forge_client` and `liquid::sync_endpoint`. Changes to the
panel belong in `src/app.cpp`; diff and anchor state belong in `ReviewModel`; scheduling and
snapshot publication belong in `ReviewController`.
