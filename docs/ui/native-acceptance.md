# Native editor acceptance

Issue #56 adds a display-required acceptance driver alongside the fixed-scale fixture gallery.
The driver opens a real GPU window and creates a disposable repository through the pinned
libliquid. It sends pointer, key and text events into SDL's queue. Before it sends text, it
requires `SDL_TextInputActive(window)`: an inactive editor session fails the test instead of
allowing injected text to conceal the lifecycle defect. `App::run` consumes the events through the
production dispatch path. It does not insert text directly, invoke
navigation actions directly, or seed editor buffers and panel selections.

`UiFrameLayout::controls` records current repository rows and Quick Open controls. Pointer
positions derive from those reported rectangles, panel tab rectangles, and the source tab's
actual layout. The shared `ui_property_assert.hpp` helpers check framebuffer coverage, shell
controls, editor bounds and source ink. The driver does not add fixed sample rectangles.

## CTest scenarios

| Scenario | Input and assertion | Current result |
|---|---|
| `launch-1` | Native drawable density, launch coverage, repository open, uploaded fonts, editor ink, resize, modal routing | Skipped when no attached display has density 1; see [density-1 acceptance owed](#density-1-acceptance-owed) |
| `launch-2` | Native drawable density, launch coverage, repository open, uploaded fonts, editor ink, resize, modal routing | Passes on the attached Retina displays |
| `edit` | Click repository row, focus and type into Quick Open, choose result, click editor, send UTF-8 text, save with Primary+S, compare persisted blob bytes through an independent handle | Passes when the navigation index loads: the focused editor restarts SDL text input after an ImGui field stops it (liquify #39). Intermittent on the current liquid pin, see [below](#quick-open-and-edit-on-the-current-liquid-pin) |
| `quick-open` | Primary+P, path text, Down, Enter; require the requested file and closed popup | Passes when the navigation index loads: the query field takes focus on open, Up/Down move the selected result, Enter opens it (liquify #33). Intermittent on the current liquid pin, see [below](#quick-open-and-edit-on-the-current-liquid-pin) |
| `palette` | Primary+Shift+P, query; require the first result selected, Down and Up to move the selection, and Enter to run it (the command's state change) | Expected to pass: the palette dispatches its selected row since liquify #34; checked by hand in the real window, the suite not yet run (tests run at release) |
| `rebind` | Primary+A selects the buffer; rebind Select All to F6 through the Keymap field's handler; require F6 to select the buffer and the old Primary+A not to; require the palette's first row for "Select All" labelled F6 and Enter on it to select the buffer; on macOS require the native Edit menu item to show F6, the palette item to show Cmd+Shift+P, and choosing the item to select the buffer; rebind to Primary+L and require the conflict reported and the chord to run neither command; restore Primary+A | Passes on the attached Retina display (liquify #34) |
| `commands` | Open a file and run every built-in command through the one dispatcher (Quit, Close File and Reveal excepted, as they open native dialogs or the file manager); require each to have a handler and to act or put its reason in the status bar | Not yet run (tests run at release) |
| `modal-input` | Focus source, open the palette, type a query, and verify only the query changes | Passes: text is not delivered to the editor while a modal is open (the modal half of liquify #39) |
| `tabs` | Click every reported visible tab and require selected state plus visible content | Passes: Build, Diagnostics, Assistant, Plugins and Structural editing |
| `shell-sizes` | At UI sizes 14, 24 and 32, light and dark: require disjoint bottom-tab rectangles at least as tall as the font; click every tab in both directions; Tab through the row and Space on each focused tab, requiring that tab selected and the editor unchanged; require the status row's controls inside the row and apart; save each framebuffer | Passes (2026-11-15, Retina). On a loaded host a synthetic click or Space is now and then dropped, as in `tabs`; the scenario repeats such an action once and logs it, so a real defect still fails |
| `daemon-failure` | Hold the repository's daemon lock with nothing listening, open a file; require the snapshot's `start_failed`, the repository details and the status row naming the failure; F7, require the build error and the status row in the same words naming Build; at 520 pixels wide and UI sizes 14 and 24, require the failure readable and opening Build, and Details present; save light and dark | Passes (2026-11-15, Retina) |
| `build` | F7 and F9 through the real daemon path; require honest unavailable and offline state, each failure once in Build with no absolute path, and the status bar naming and opening Build; save light and dark framebuffers | Passes; successful sandboxed build execution is owed on a macFUSE-capable host |
| `cut` | Click a file, click the editor, type, save with Primary+S, open Cut Change with Primary+Shift+K, check the listed drips and files, type a message, click Cut change; reopen the repository and require the change in liquid's log with the message, this computer's identity and the typed edit; save light and dark framebuffers | Passes ([commands](commands.md), [cut](../editor/cut.md)) |
| `opened-clone-push` | Clone from a test lake with libliquid, open the folder, type, save, cut, F9; require every reference applied and a fresh clone serving the edit; save light and dark | Passes ([build](../editor/build.md)) |
| `set-lake-push` | F9 with no lake, click Set lake in the push failure, type the address, F9 again; require the push to reach that lake and its refusal (another repository's history) worded as such | Passes |
| `request-review` | A repository with no lake shows Set lake in Review; a folder cloned from a test lake shows the teaching empty state with Request review; type, save, cut, push, click Request review, type reviewers, submit; require the review opened and the test lake holding it for the current change with those reviewers; save light and dark | Passes ([review](../editor/review.md)) |
| `front-door-actions` | No repository; require Open folder, New repository, Clone from lake, New window and Your identity; open and cancel the clone dialog; read Your identity; New repository, type a folder, Create repository; require it open and readable by libliquid; save light and dark | Passes ([first run](first-run.md)) |
| `set-up-build` | A CMake repository with no recorded build: F7, open Build from the status bar, Set up build, require the CMake proposal, Record and build; require the next F7 to find the recorded build; save light and dark | Not yet run (tests run at release) |
| `table-headers` | Open the palette and return to the front door with a recent repository; require each header row; save light and dark | Not yet run (tests run at release) |
| `plugin-hang` | The fault plugin in the user's global plugin list, in its host process; require its hanging panel's loading state, run `fault.sleep` from the palette and require Enter to return and the status to say it is running; require the panel's and the command's deadline errors on screen, and no four-frame pulse slower than one second; save the loading and answered framebuffers | Passed 5 of 6 runs by hand on macOS (slowest pulse 67 to 83 ms; the panel and the command each ended at their deadline). The sixth failed before the window was asked anything: reading the new repository's plugin list met the store's lost index run (liquid#526), so no plugin loaded ([plugins](../plugins/README.md#requests-off-the-ui-thread)) |
| `look` | The empty editor, palette, Settings, editor and front door; require the title sizes; save light and dark | Not yet run (tests run at release) |
| `front-door-clone-push` | Clone from the front door, type, save, cut, F9 | Disabled, liquify #97 (liquid#526): the first save into the just-cloned store usually loses an index run, and now and then does not, so neither result is stable. Run it by hand with `build/tests/liquify-native-acceptance front-door-clone-push <dir>` |
| `front-door-clone-failures` | No repository; click Clone from lake and its Clone repository button against a loopback responder (401, 403, 404, 503, 400, `https` to a plain socket) and a closed port; require each failure's kind, phase, sentence and Details; save light and dark framebuffers | Passes ([errors.md](errors.md)) |

Both launch scenarios use `App::init()` with the normal high-density window flag. If the
requested density is absent on the launch display, the driver moves the same window across the
attached displays and measures its actual drawable density. Density 1 skips with its recorded
display reason when unavailable. It does not disable high-density rendering or change renderer
scale to manufacture coverage.

The `WILL_FAIL` entries (none at present) stay green only while their linked issue remains open. A fix makes its
test pass and CTest then reports the stale annotation. The display prerequisite is a skip, not a
`WILL_FAIL`, because it does not describe a product defect. No product behavior is changed to
satisfy an assertion.

## Quick Open and edit on the current liquid pin

The `quick-open` and `edit` scenarios lost their `WILL_FAIL` with the liquify #33 and #39 fixes.
They were checked by reading the code and by real-window runs of those two scenarios only, not by
the suite (tests run at release). Without the editor's text-input restart, `edit` stops with
`input_active=0` after Quick Open closes; with it, the typed bytes persist through Save.

On the current liquid pin (`27d56ba`) both scenarios still fail intermittently, for a reason
outside liquify: libliquid's shared `MANIFEST.backend-tmp` temporary races between the handles
liquify opens on one repository, the navigation index reports `in rename: No such file or
directory`, and Quick Open has no paths to offer. liquid#526 fixed that on liquid's `dev`
(liquid#532); liquify #97 tracks taking it. Built against liquid `dev` (`79e4325`), both scenarios
passed every direct run. Under CTest on a heavily loaded host a pointer click in the shared
open-file step sometimes does not land, with either liquid.

## Evidence required for UI changes

A UI change needs both relevant before/after [fixture images](README.md) and a fresh live-window
walkthrough with its event transcript and resulting state. Fixture images prove deterministic
rendering at their declared scale; native acceptance proves only the specific event and repository
paths it drives. Neither replaces the other. Performance smoke remains synthetic input coverage.

## Live-window walkthrough: 2026-09-08

The native static build configured and compiled successfully with at most six jobs. The real
Cocoa/Metal window passed `liquify-native-acceptance-launch-2`, including opening the persisted
source by pointer, shared editor/toolbar properties, uploaded font density, resize and opening
its modal through the keyboard. Its initial measurements were:

```text
native window: driver=cocoa renderer=metal density=2.0x2.0 hidden=0
launch: scale=2.0x2.0 logical=2240x1197 drawable=4480x2394 coverage=0.963
native acceptance: PASS launch-2
100% tests passed, 0 tests failed out of 1
```

The full native workflow run reproduced #33 and #39, reproduced palette dispatch issue #34, and
passed every visible panel-tab selection. The build run used F7 and F9 against the real disposable
repository and observed the actual unavailable daemon and absent configured remote:

```text
build: running=0 actions=0 result='' error='editor: build daemon is unavailable'
offline: running=0 objects=0 error='... repository has no configured sync remote'
native acceptance: PASS build
```

The isolated unmodified density-1 check could not reach its required density:

```text
native display: name=32Q1U drawable_density=2.0x2.0 requested=1
native display: name=Built-in Retina Display drawable_density=2.0x2.0 requested=1
SKIP: unmodified native launch cannot reach drawable density 1; attached displays are density 2.0x2.0
100% tests passed, 0 tests failed out of 1
```

The one-minute host load after that check was 25.27. The density-2 result is evidence against the
#31 symptom on the hardware where it was reported: an unmodified `App::init()` window measured
2.0×2.0 drawable density and 0.963 coverage, rather than a quarter-size shell. This does not
close or change #31.

The input transcript was:

```text
Quick Open: Primary+P → text other.cpp → Down → Enter
Palette: Primary+Shift+P → text Toggle bottom panel → Down → Enter
Editor: Quick Open input → pointer result → source tab → pointer editor → UTF-8 text → Primary+S → read blob
Tabs: pointer Build → Diagnostics → Assistant → Plugins → Structural editing
Build: F7 → unavailable state; F9 → offline state
```

The #39 editor step stops before Save because `SDL_TextInputActive(window)` is false after selecting
the Quick Open result; the test will compare the persisted bytes after the issue is fixed. The
#39 modal test also shows the palette query changing the obscured source from 62 to 73 bytes.
The Quick Open query test exposes #33 before it can select a result. #35 does not reproduce:
all five visible tabs selected their reported panel content.

## Density-1 acceptance owed

`liquify-native-acceptance-launch-1` returns 77 and CTest records a skip when every attached
display reports a different density. On this host, `32Q1U` and `Built-in Retina Display` each
reported `drawable_density=2.0x2.0`. Density-1 acceptance remains owed until a non-Retina display
or a machine that can request an actual density-1 mode is available. Until then, density-2 launch
is the only launch acceptance this hardware can give. This is not a `WILL_FAIL` condition.

## Build-execution acceptance owed

Successful and failed sandboxed build execution requires macFUSE on macOS. This build found no
macFUSE installation, and liquid reports that condition instead of executing an action. The native
`build` scenario still proves the app's actual F7 unavailable transition and F9 offline transition.
The controlled successful action, failed action and their diagnostics remain owed on a macFUSE-capable
macOS host or the Linux lane with libfuse3. Linux proof is deferred here because the prescribed
`linux-proof.sh` helper is absent.
