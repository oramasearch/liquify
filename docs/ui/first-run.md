# First run and repository lifecycle

Liquify starts at a repository front door when no path is supplied. The front door can open a
local folder, clone a lake repository into a new local folder, reopen a recent repository, create
another Liquify window, or establish a local TAAP identity session. Opening another repository
replaces the active repository services in the same process; no restart is required.

The lifecycle is a plain C++ state machine. `welcome`, `opening`, `cloning`, `open`, `unreadable`,
`offline`, and `failed` are explicit states so a missing or unreadable local repository is not
presented as a network failure. Lake unavailability is an expected offline state. A failed clone
keeps its source and destination available for retry.

Recent repositories store the path, display name, branch, last named change, last-change date,
and last-opened time. The list is capped at eight entries and saved under the liquid home directory
in `liquify/first-run.json`. Identity persistence contains only the public actor identifier and its
custody description. Tokens and private key material are never written to this file.

The bottom status bar is derived from a separately tested model. It always accounts for daemon,
sync, hydration, build, presence, current change, branch, notifications, and identity session.
When no repository is active it also says so explicitly.

The deterministic screenshot registry covers the front door and both recovery states in light and
dark themes. `welcome-*` shows recent repositories and the identity action;
`repository-unreadable-*` shows local recovery; and `lake-offline-*` shows the retryable offline
clone state.
