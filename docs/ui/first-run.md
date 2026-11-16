# First run and repository lifecycle

Liquify starts at a repository front door when no path is supplied. The front door can open a
local folder, clone a lake repository into a new local folder, reopen a recent repository, create
another Liquify window, or establish a local TAAP identity session. Opening another repository
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

The deterministic screenshot registry covers the front door and both recovery states in light and
dark themes. `welcome-*` shows recent repositories and the identity action;
`repository-unreadable-*` shows local recovery; and `lake-offline-*` shows the retryable offline
clone state. The native acceptance scenario `front-door-clone-failures` shows every clone failure
class in the real window, in both themes.
