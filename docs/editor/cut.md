# Cut Change

Every save in liquify is a **drip**: one captured, signed write. A **cut** names the drips saved
since the last change as one **change**, the unit lake pushes, lists and reviews. Cut Change makes
the same change `liquid cut <message>` does, from the window.

## Using it

- **Cut Change** is in the command palette, in the **Change** menu on macOS, and on
  **Primary+Shift+K** (Cmd+Shift+K on macOS, Ctrl+Shift+K elsewhere). The binding can be changed in
  the keymap like any other (`change.cut`).
- The composer says what it gathers before anything is cut: the change the work builds on (or that
  this is the repository's first change), how many drips and which files, and, under **Drips**,
  each drip's short id, time and files.
- An editor with unsaved edits is named in a warning: those edits are not in the change until they
  are saved.
- **Message** sits above its field and takes the cursor when the composer opens. The first line is
  the change's title; it must not be empty. **Cut change** is the one primary action; **Cancel** and
  Escape close the composer without cutting.
- While the composer is open, typing goes only to the message field, never to the source behind it.

## How it works

`include/liquify/change_cut.hpp` is plain C++ over libliquid; `src/app.cpp` draws the composer.

| Step | libliquid call | Thread |
|---|---|---|
| List what a cut gathers | `repository::open`, `repository::status` (the head), `repository::drips` | worker (`std::async`) |
| Cut | `repository::open` with the window's multiplayer session, `repository::cut(message)` | worker |

`repository::drips` returns every recorded drip; a drip records the change it was written on, so
the composer keeps those written on the current head (on no change, in a new repository). The cut
is signed by this computer's identity (`identity_detail::local_signer`) and recorded on `main`,
exactly as the CLI does. A failure is shown with the error component of
[`../ui/errors.md`](../ui/errors.md); the saved drips are untouched.

## How to verify

- `ctest -R ChangeCut`: three drips across two files are listed with their paths and author, the
  cut appears first in `repository::log` with its message and the same author, the next change
  gathers only later drips, and an empty message is refused without touching the repository.
- `liquify-native-acceptance-cut` (real GPU window): types into the editor, saves, opens the
  composer with Primary+Shift+K, checks the listed drips and files, types a message, clicks
  **Cut change**, then reopens the repository independently and requires the change in liquid's
  log with the message, this computer's identity and the typed edit. It also requires that the
  message did not reach the source. It saves `cut-composer-empty-*`, `cut-composer-*` and
  `cut-done-*` in both themes.

## How to change

The composer's text and layout are in `App::draw_cut_composer`; what it gathers is
`load_pending_change`. Keep the cut itself a single `repository::cut` call, so liquify and the CLI
never record different changes.
