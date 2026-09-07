# Navigation

Navigation is built from libliquid repository and symbol records. `RepositoryView` opens the
repository on a worker, walks the complete tree, and publishes a path index and symbol index to
the application. The render thread only reads completed snapshots. It does not scan a host
working directory or maintain a second source database.

## Repository tree

Each row identifies its file kind and may add two independent annotations:

- drive state from libliquid's drive status: `online only`, `offline`, or `not here`;
- change state from repository records: `modified`, `in change`, or `conflicted`.

`modified` compares the current root with the recorded head snapshot, `in change` compares the
latest recorded head with its first parent, and `conflicted` comes from repository status. The
states have a fixed priority for the compact row label: conflicted, modified, in change, clean.
Drive annotations are omitted when no drive is configured.

The row context menu opens a blob, renames or moves a path, creates a file beside the selected
path, or reveals the checkout in the platform file manager. Rename and move use
`liquid::repository::move`; new file uses `repository::write`. Each completed mutation refreshes
both the visible root and the navigation index.

## Paths, symbols, and locations

Quick Open ranks file paths with a case-insensitive subsequence matcher. Contiguous characters,
component starts, exact matches, prefixes, and filename matches receive higher scores; shorter
paths and then lexical order break ties. Directories are excluded from results.

Symbol Search ranks definitions from libliquid symbol records. It never reparses source in the UI.
The same records support Go to Definition and Find References. Call Hierarchy uses a record's
container symbol when the graph supplies one, so callers and callees are shown only when that
relationship exists. Results have stable path-and-location ordering.

The editor breadcrumb contains each path component and the smallest definition enclosing the
cursor. Opening a path or symbol records its line and column. Open Recent is most-recent-first;
Back and Forward retain the cursor location and clear the forward list when a new branch is
visited.

## Commands and default keys

The navigation registry is plain C++ and supplies both the command palette and default bindings.
`Primary` means Command on macOS and Control elsewhere.

| Action | Default |
|---|---|
| Quick Open | `Primary+P` |
| Symbol Search | `Primary+T` |
| Go to Definition | `F12` |
| Find References | `Shift+F12` |
| Call Hierarchy | `Alt+F12` |
| Focus Breadcrumbs | `Primary+Shift+.` |
| Open Recent | `Primary+R` |
| Back | `Alt+Left` |
| Forward | `Alt+Right` |
| Open | `Enter` |
| Rename | `F2` |
| Move | `Primary+Shift+M` |
| New File | `Primary+N` |
| Reveal | `Primary+Alt+R` |

The repository-wide configurable command system remains tracked by issue #34. This page records
the navigation registry consumed by the current palette and key handler.

## Verification and screenshots

`test_navigation.cpp` covers file kinds, path and symbol ranking, identifier lookup, definition
and reference order, caller/callee construction, breadcrumbs, history, and unique bindings.
`test_repository_view.cpp` creates repositories through libliquid and checks indexed paths,
symbols, change annotations, move, and new-file refresh. `test_screenshot.cpp` checks the
annotated repository tree and Quick Open surface.

The deterministic navigation state is committed in both themes. Screenshot capture uses a fixed
UI time step, and its populated Quick Open field is read-only, so popup fading, input focus, and
cursor blinking cannot alter the reference pixels.

| Light | Dark |
|---|---|
| [navigation-light.png](screenshots/navigation-light.png) | [navigation-dark.png](screenshots/navigation-dark.png) |

Refresh the complete screenshot registry with
`LIQUIFY_BINARY=./build/liquify ./tools/ui/screenshot.sh`.
