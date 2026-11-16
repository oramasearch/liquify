# First run and repository lifecycle

Liquify starts at a repository front door when no path is supplied. The front door can open a
local folder, create a new repository, clone a lake repository into a new local folder, reopen a
recent repository, create another Liquify window, or show **Your identity**.

## Layout

The front door is laid out as lake's pages are (liquify#126; lake's `docs/ui/front-door.md` and
`docs/ui/design-system.md`), on the `canvas` role with the block centred vertically while it fits:

- **The column** (a third of the width, 340 to 420 pixels): liquify's mark (a white drop on an
  `action` tile) and name, the eyebrow *THE IDE FOR LIQUID* (12, tertiary ink), the title
  *Welcome to liquify* and one line of supporting copy. Under it the three ways in are large rows
  in one panel (`surface`, a hairline border, `panel_radius`): **Open a folder**, **New
  repository** and **Clone from lake**, each a 32-pixel icon tile, a label and a one-line
  description; the whole row is the target, with `layer_hover` on hover and hairlines between
  rows. Open a folder is the front door's one primary action, so its tile is filled with `action`
  and its label is in `action`; the others use the soft tint. **Your identity** and **New window**
  are quiet compact buttons under the panel, with a caption that says what the identity is.
- **Recent Repositories** (the rest of the width): a heading, a line of copy and a panel holding a
  compact table with one row per repository: Repository (folder icon and name), Branch, Last change
  (identifiers shortened, the full value on hover) and Updated (Today, Yesterday, *n* days ago,
  then the date). Hovering a row shows its folder as a tooltip and an **Open in new window** quiet
  button at its end; clicking anywhere else on the row opens it in this window.
- **The empty state**, when nothing has been opened, is one panel that says what is absent (*No
  recent repositories*), what will appear there, how to start (open a folder or clone from lake),
  and that `liquify <path>` opens a repository from a terminal.
- Below 760 pixels of content width the column and Recent Repositories stack.
- An open failure, a clone failure, or an opening or cloning repository is shown in the column,
  above the ways in.

The control ids the native driver uses are unchanged: `front-door/open`, `front-door/new`,
`front-door/clone`, `front-door/new-window`, `front-door/identity`, `welcome/title`,
`welcome/copy`, `welcome/recent-heading` and `recent-repositories/header`. New ones:
`welcome/eyebrow`, `front-door/recent/<name>` for each row (and `.../new-window` while its button
shows) and `front-door/recent-empty`.

The **Clone from Lake** and **New Repository** dialogs follow lake's forms: supporting copy in the
secondary ink, each label above its field, hints as captions under the field, and the buttons at
the bottom right with the primary last.

**New repository** opens a dialog with a Folder field (and Choose… for the native folder picker).
Create repository calls libliquid's `repository::create` on a worker, then opens the repository in
the same window. A folder that already holds a liquid repository is refused with "open it instead".

**Your identity** is this computer's TAAP signing identity, the key that signs every drip and
change; it is not a lake account, and the button no longer says "Sign in to lake". It reads (or
creates) the identity through `identity_detail::local_signer`, and its popover shows the identifier
and where the key is held, with **Forget**, which removes it from the front door only. Signing in to a lake comes
with choosing one (liquify#96).

**Clone from Lake** asks for the lake repository address (pasted from the repository's page on
lake) and a new folder, each with its label above the field, and says the folder must not exist
yet or be empty. Opening another repository
replaces the active repository services in the same process; no restart is required. The active
repository name in the status bar returns to this front door, while each recent row opens in the
current window on a click, or in a new one from its hover action.

The lifecycle is a plain C++ state machine. `welcome`, `opening`, `cloning`, `open`, `unreadable`,
`offline`, and `failed` are explicit states so a missing or unreadable local repository is not
presented as a network failure. A failed clone is classified from liquid's error code
([errors.md](errors.md)): only a lake that could not be reached is `offline`, which is an expected
state ("working offline is normal"); a lake that answered (a TLS failure, a refused token, no such
repository, a busy or unsupported lake) is `failed`, with its own sentence and next step, and
liquid's message behind Details. A failed clone keeps its source and destination available for
retry.

Clone uses libliquid's public drive client: configure the remote, pin the root, synchronize once,
and refuse a rejected reference update. The optional bearer token comes from `LIQUID_TOKEN` and is
never copied into UI state. Targets must be absent or empty directories and cannot be symbolic
links. Failure removes only files created by that clone attempt; a pre-existing empty directory is
left in place.

Recent repositories store the path, display name, branch, last named change, last-change date,
and last-opened time. The list is capped at eight entries and saved under the liquid home directory
in `liquify/first-run.json`. Identity persistence contains only the public actor identifier and its
custody description. Tokens and private key material are never written to this file.

The bottom status bar is derived from a separately tested model. It always accounts for daemon,
actionable sync, hydration, build, and failure state. Identity, current-change, validation, timing,
and collaboration records are available from deliberate details, panel, and gutter affordances.
When no repository is active it also says so explicitly.

The daemon state comes from the editor graph's snapshot of the repository's build service, never
from whether a graph exists (liquify#36). `DaemonState` is `starting` until the start or attach
answers, then `running` or `stopped` from each status call, `start_failed` when neither starting
nor attaching worked, and `disconnected` when a status call fails after the service once answered.
The last two keep their message in `daemon_error`, which a later successful query does not clear.
Repository details then read `Daemon failed to start` or `Daemon connection lost`, and the status
row shows the failure as "The build service is not running for this repository" (the Build panel's
words for a build that found no service) or "The build service stopped answering", opening
Diagnostics.

The status row is one measured line. Its height follows the UI font (32 pixels at the default
size, growing with the compact buttons when the font is enlarged), and its segments give way in
priority order when the width runs out: Details and a failure stay and elide last, then the
repository name, the cursor position and the branch; plain status text and the collaborator count
elide first. An elided segment shows its full text as a tooltip. The native acceptance scenario
`daemon-failure` holds the repository's daemon lock with nothing listening, so the service cannot
start, and requires the snapshot, the repository details, the status row and the F7 build error to
agree, then narrows the window to 520 pixels at UI sizes 14 and 24 and requires the failure to stay
readable and still open Build; `shell-sizes` checks the row's controls at UI sizes 14, 24 and 32 in
both themes.

Repository summaries come from libliquid's repository status, refs, change log, active presence,
and drive status APIs. This supplies the current change and branch, last named change and date,
remote bytes, hydration debt, pending uploads, offline reason, and active collaborator count.

The native acceptance scenario `front-door-actions` drives every front-door action in the real
window, creates a repository with no terminal, and saves `front-door`, `front-door-clone`,
`front-door-identity`, `front-door-new` and `front-door-created` in light and dark.

The deterministic screenshot registry covers the front door and both recovery states in light and
dark themes. `welcome-*` shows recent repositories and the identity action;
`repository-unreadable-*` shows local recovery; and `lake-offline-*` shows the retryable offline
clone state. The native acceptance scenario `front-door-clone-failures` shows every clone failure
class in the real window, in both themes.
