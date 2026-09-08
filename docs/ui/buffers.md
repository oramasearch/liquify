# Open buffers and unsaved work

Each repository window retains an in-memory buffer for every open repository path. Opening a
file from the tree or Quick Open activates that path's existing buffer, including its exact text,
selection, secondary carets, local undo/redo history and scroll position. It does not reload the
saved blob. Paths are normalized before lookup. An explicit navigation location, such as a symbol
or history entry, still moves the cursor to that location.

Switching files never prompts, saves or discards text. The source tab displays the active path and
its dirty marker; the repository tree and Quick Open activate other retained buffers. Read-only
review diffs use separate storage and do not replace repository buffers.

## Close, repository switch and quit

Close File (`Primary+W`, also the File menu and command palette) closes the active source buffer.
Closing a clean buffer needs no confirmation. Closing a dirty buffer opens a native
Save / Discard / Cancel dialog. Repository replacement and window close use the same dialog for
all dirty buffers, including inactive files; the dialog names every affected path.

- **Save** writes every affected buffer through libliquid as a drip before continuing. If a save
  fails, the action stops, all buffers remain open, and the error is shown. Earlier successful
  saves remain persisted.
- **Discard** explicitly permits removal of the affected in-memory buffers.
- **Cancel**, Escape and dismissing the dialog leave all buffers intact. Text, cursor, selections,
  undo/redo and the active buffer do not change. Cancelling a repository switch retains the current
  repository and its services.

A repository is loaded before asking to replace the current one, so a failed open never destroys
unsaved work. The confirmation happens at activation, after loading, and includes any edits made
while the replacement loaded. Merely visiting the repository front door does not close buffers.

Known persisted structural edits and file imports refresh clean open buffers explicitly, retaining
local history. Dirty open buffers retain their text when persisted content changes.

Undo and Redo operate on the active buffer's local edit history. Selection replacement,
multi-cursor edits and replacement commands are single history entries; saving keeps this history.
Undoing back to the saved text clears the dirty marker, while undoing a saved edit makes the buffer
dirty again. Repository operation history remains available through libliquid's graph API.

## Crash recovery policy

Unsaved buffers and local undo/redo exist only in process memory. A crash, forced termination or
power loss loses text that has not been saved. There is no autosave, recovery journal or restored
open-buffer session. Saved drips persist through libliquid and its repository history; they cannot
recover text that was never saved. Normal close and quit are protected by the native dialog.

## Regression proof

`liquify-native-buffer-regression` is a display-required macOS regression linked to the same
application and pinned libliquid as the editor. It creates a fresh repository, delivers text and
key events through the native application's event loop, and clicks actual native dialog buttons.
It checks buffer identity and state directly after rendering. It is separate from headless ctest
and does not use the screenshot harness or change render scale.

```sh
LIQUID_HOME="$PWD/.cache/issue-55/home" \
  ./build/tests/liquify-native-buffer-regression \
  "$PWD/.cache/issue-55/native-fixture"
```

On macOS CTest registers this as `liquify-native-buffer-regression`; it creates its fixture under
the build directory and removes it after the run. If a native window cannot be initialized, the
test reports a clear skip rather than a failure. Proof output and the before/after record are kept
with the pull request for Refs #55. Rendering/scale, general native acceptance coverage and status
bar layout are tracked separately.
