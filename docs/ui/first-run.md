# First run and repository lifecycle

Liquify starts at a repository front door when no path is supplied. The front door can open a
local folder, create a new repository, clone a lake repository into a new local folder, reopen a
recent repository, create another Liquify window, or show **Your identity**.

**New repository** opens a dialog with a Folder field (and Choose… for the native folder picker).
Create repository calls libliquid's `repository::create` on a worker, then opens the repository in
the same window. A folder that already holds a liquid repository is refused with "open it instead".

**Your identity** is this computer's TAAP signing identity, the key that signs every drip and
change; it is not a lake account, and the button no longer says "Sign in to lake". It reads (or
creates) the identity through `identity_detail::local_signer`, and its popover shows the identifier
and where the key is held. Forget removes it from the front door only. Signing in to a lake comes
with choosing one (liquify#96).

**Clone from Lake** asks for the lake repository address (pasted from the repository's page on
lake) and a new folder, each with its label above the field, and says the folder must not exist
yet or be empty. Opening another repository
replaces the active repository services in the same process; no restart is required. The active
repository name in the status bar returns to this front door, while each recent row can open in the
current window or a new one.

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
