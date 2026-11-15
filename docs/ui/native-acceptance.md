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
| `edit` | Click repository row, focus and type into Quick Open, choose result, click editor, send UTF-8 text, save with Primary+S, compare persisted blob bytes through an independent handle | `WILL_FAIL`, liquify #39 |
| `quick-open` | Primary+P, path text, Down, Enter; require the requested file and closed popup | `WILL_FAIL`, liquify #33 |
| `palette` | Primary+Shift+P, query, Down, Enter; require the command's state change | `WILL_FAIL`, liquify #34 |
| `modal-input` | Focus source, open the palette, type a query, and verify only the query changes | `WILL_FAIL`, liquify #39 |
| `tabs` | Click every reported visible tab and require selected state plus visible content | Passes: Build, Diagnostics, Assistant, Plugins and Structural editing |
| `build` | F7 and F9 through the real daemon path; require honest unavailable and offline state | Passes; successful sandboxed build execution is owed on a macFUSE-capable host |
| `front-door-clone-failures` | No repository; click Clone from lake and its Clone repository button against a loopback responder (401, 403, 404, 503, 400, `https` to a plain socket) and a closed port; require each failure's kind, phase, sentence and Details; save light and dark framebuffers | Passes ([errors.md](errors.md)) |

Both launch scenarios use `App::init()` with the normal high-density window flag. If the
requested density is absent on the launch display, the driver moves the same window across the
attached displays and measures its actual drawable density. Density 1 skips with its recorded
display reason when unavailable. It does not disable high-density rendering or change renderer
scale to manufacture coverage.

The `WILL_FAIL` entries stay green only while their linked issue remains open. A fix makes its
test pass and CTest then reports the stale annotation. The display prerequisite is a skip, not a
`WILL_FAIL`, because it does not describe a product defect. No product behavior is changed to
satisfy an assertion.

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
