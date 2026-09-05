# The editor surface

**Decision.** The editor buffer is a retained GPU text layer inside Dear ImGui (bridge decision
15). ImGui owns the window, repository panel and controls. `src/app.cpp` uses the SDL renderer
directly for buffer text, selection and cursor; no other translation unit includes SDL or ImGui.

## Buffer and liquid

`EditorBuffer` is a plain-C++ line buffer. Its positions are a line number and UTF-8 byte boundary;
movement and deletion preserve codepoint boundaries. Insertions and replacements return the exact
range of removed and inserted lines. The buffer remains in memory until save.

Opening a root blob calls `liquid::repository::open` and `repository::read` on a worker. Saving
copies the current bytes to a worker and calls `repository::write`, so the write follows
libliquid's drip path. The UI never writes repository files directly. The repository and blob path
stay attached to the buffer; a generated smoke buffer deliberately has no save target.
The save result retains the repository's pending-drip count before the repository handle closes,
so the editor status does not reopen and checkpoint the journal merely to display it.

The default mode is standard, non-modal editing. Arrow, Home, End, Page Up, Page Down, selection,
Backspace, Delete, Return and text input edit directly. The mode button or primary-modifier+M
switches to modal command mode; `i` and `a` enter insert mode, Escape returns to command mode, and
`h`, `j`, `k`, `l`, `0`, `$` and `x` provide its initial command set. Primary-modifier+S saves in
either mode.

## Retained rendering

`TextLayoutCache` owns one optional layout per line under a blob identity. A layout decodes UTF-8,
expands four-column tabs and records byte-to-column positions. Repeated viewport visits reuse the
same layout. A buffer edit erases only the replaced line entries, inserts empty entries for new
lines and shifts untouched entries intact.

`RetainedTextLayer` turns each visible cached layout into textured glyph quads once and retains
that geometry by layout generation. The texture is ImGui's GPU font atlas; the starting font is
monospace. Each frame culls to the visible line range, translates retained quads into one reused
vertex/index buffer, clips to the ImGui editor rectangle and makes one SDL geometry submission.
Selection rectangles are drawn before the glyphs and the standard bar or modal block cursor after
them. Scrolling changes the first visible line without laying out the other 49,000-plus lines.

## Smoke measurement

`liquify --smoke [N]` creates exactly 50,000 lines, advances through the entire buffer, inserts one
character into the current visible line per frame, renders through the retained layer and exits 0.
It reports frame work and edit-to-present latency. The measurement includes layout invalidation,
geometry replacement, rendering and vsync; it excludes one-time buffer construction before the
window loop.

Measured on 2026-09-04 on this machine's 60 Hz display with the macOS release build:

```text
$ ./build/liquify --smoke 3000
smoke: lines=50000 frames=3000 visible_lines=68 avg_frame_ms=16.778 p95_frame_ms=18.885 achieved_hz=59.6 display_hz=60.0 avg_typing_ms=16.925 p95_typing_ms=18.909
```

The average scroll rate is the display refresh rate. Typing latency is one presented frame on
average while the viewport crosses the full 50,000-line buffer.

## Interactive analysis

[`interactive-analysis.md`](interactive-analysis.md) records the graph-backed analysis service.
Open C and C++ buffers are volatile overlay inputs with retained error-tolerant trees; edits use
tree-sitter's incremental parse and cancel active queries. Each buffer mutation computes the exact
changed byte span and queues it off the render thread. Closed files contribute stubs, optional
compiler symbol records take precedence, and build diagnostics are read by translation unit.

F12 and the Definition button demand go-to-definition; F1 and the Hover button demand hover;
primary-modifier+Space and the Complete button demand completion. Definition results open their
target blob and select the declaration. Completion results appear in the analysis panel and insert
their candidate when selected. Hover shows the signature and leading documentation above the
buffer.

Diagnostics are demanded after buffer open and every edit. The retained renderer draws a colored
underline at each source range, while the diagnostics panel lists severity, message and the
measured edit-to-result duration. Selecting a diagnostic opens its blob and moves the cursor to its
range. The build diagnostic producer is the `EditorGraph::publish_build_diagnostics` boundary;
launching inline builds remains liquify #6.

The status line is formatted from `liquid::daemon_status`: the last workspace drip's invalidated
paths, the sum of target cursor distances as the validation backlog, daemon availability and the
pending repository drip count returned by the last editor save. Recursive repository navigation
loads directory trees lazily as recorded in [`../app-shell/README.md`](../app-shell/README.md).

## Verify

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/liquify version
./build/liquify --smoke
```
