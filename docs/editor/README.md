# The editor surface

[`../ui/editor.md`](../ui/editor.md) records the editor view's multi-cursor and selection model,
syntax colors and folds, line and collaboration gutters, search/replace, operation-log history and
light/dark screenshot states.

[`ai.md`](ai.md) records the optional assistant, its offline switch guarantee, graph-derived
inspectable context, provider contract, keyboard commands, and accept-only edit path.
[`review.md`](review.md) records the lake review panel, retained inline text diff, cursor anchors,
rebase mapping states, review controls, and keyboard commands.

[`structural-editing.md`](structural-editing.md) records syntax-node selection, canonical edit
records, rename and extract-function previews, the headless agent surface and their limits.

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

[`../multiplayer.md`](../multiplayer.md) records the presence, attribution, and lease controls that
surround the buffer. Editor saves reopen liquid with the active multiplayer session, preserving
liquid's authorization boundary for a held lease.

The default mode is standard, non-modal editing. Arrow, Home, End, Page Up, Page Down, selection,
Backspace, Delete, Return and text input edit directly. The mode button or primary-modifier+M
switches to modal command mode; `i` and `a` enter insert mode, Escape returns to command mode, and
`h`, `j`, `k`, `l`, `0`, `$` and `x` provide its initial command set. Primary-modifier+S saves in
either mode. SDL composition events keep CJK and dead-key pre-edit text out of the buffer until
commit. Cut and copy publish rich and plain clipboard forms; paste prefers plain text and accepts
HTML-only sources. Tab returns focus from the retained editor to the shell, while
primary-modifier+`]` inserts the repository indentation. Desktop file drops write through
libliquid at the repository root or beside the active blob. The complete contract and focus order
are in [`../ui/accessibility.md`](../ui/accessibility.md).

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

The #6 verification was measured on 2026-09-06 on this macOS host. The load average was read
immediately after the smoke run:

```text
$ ./build/liquify --smoke 30
smoke: lines=50000 frames=30 visible_lines=41 avg_frame_ms=0.445 p95_frame_ms=1.378 achieved_hz=2247.5 display_hz=60.0 avg_typing_ms=0.578 p95_typing_ms=1.540
$ sysctl -n vm.loadavg
{ 70.88 48.31 54.69 }
```

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
range. Failed actions publish their diagnostics from the daemon event stream directly to the
retained line layer.

## Build, test and deploy

[`build.md`](build.md) describes the graph-backed panels. F7 requests the configured repository
build through the in-process daemon, F8 runs registered graph tests, F9 pushes saved drips to the
configured sync remote, and primary-modifier+P opens the corresponding command-palette entries.
The status line shows active actions and the most recent result.

Configured plugin commands are appended to the keyboard-first command palette with their declared
default bindings. Data-only plugin text panels render below completion results. A panel operation
that returns an error or invalid view model displays that error in its own row without hiding other
plugin panels. The native boundary and configuration are recorded in
[`../plugins/README.md`](../plugins/README.md).

The status line is formatted from `liquid::daemon_status`: the last workspace drip's invalidated
paths, the sum of target cursor distances as the validation backlog, daemon availability and the
pending repository drip count returned by the last editor save. Recursive repository navigation
loads directory trees lazily as recorded in [`../app-shell/README.md`](../app-shell/README.md).

## Build measurement

Linux proof deferred (decision 33). The current macOS measurement is the display-backed smoke run
recorded above. It measures the editor's retained rendering and edit-to-present path while the
build, test, and deploy controller is verified by
`EditorGraph.PresentsBuildEventsTestsAndRefusedReferences`.

## Phase 4 exit measurement

`liquify-editor-graph-bench` imported liquid's own checkout into a fresh libliquid repository. It
counted 493 source-tree files while excluding `.git`, `.liquid`, submodules and build directories,
then loaded only the 17 root entries. The measurement held link completion until the editor graph
returned its first diagnostic, proving that the diagnostic result did not wait for relinking.

Measured on 2026-09-04 on the same machine as the retained-rendering baseline:

```text
$ ./build/liquify-editor-graph-bench modules/liquid build/liquid-editor-measure-repository-2
editor graph: liquid_files=493 root_entries=17 repository_open_ms=221.276 root_read_ms=220.319 edit_to_diagnostic_ms=10.315 diagnostic_before_relink=true

$ ./build/liquify --smoke 3000
smoke: lines=50000 frames=3000 visible_lines=68 avg_frame_ms=16.821 p95_frame_ms=18.317 achieved_hz=59.4 display_hz=60.0 avg_typing_ms=16.974 p95_typing_ms=18.388
```

The lazy root open remains independent of repository size. Average typing latency changed from
16.925 ms in the baseline above to 16.974 ms with the graph integration, a 0.049 ms difference;
both measurements present an edit in one display frame on average.

## Assistant context measurement

`liquify-ai-context-bench` loads liquid's C and C++ declarations into `InteractiveAnalysis`, opens
`src/repository/repository_impl.cpp`, selects its `repository` definition, resolves 20 referenced
definitions through the graph, and assembles the structured context 1,000 times. The comparison
uses the source file bytes as the naive whole-file context. Build benchmarks with
`-DLIQUIFY_BUILD_BENCH=ON`.

Measured on 2026-09-05 on the same machine as the editor measurements above:

```text
$ ./build/liquify-ai-context-bench modules/liquid build/ai-context-state
ai context: definition=repository references=20 bytes=3215 whole_file_bytes=275451 percent_of_whole_file=1.16718 assembly_us=69.522
```

The graph context is 1.17% of the naive file payload and names every omission when a configured
budget excludes a fact.

## Review measurement

`liquify-review-bench` imports 50 C++, header, and Markdown files from liquid's own tree into one
change, builds the retained-layer diff rows, then maps 200 anchors in
`src/repository/repository_impl.cpp` after inserting a line above them. Build benchmarks with
`-DLIQUIFY_BUILD_BENCH=ON` and use a new output repository for each run.

Measured on 2026-09-06 on the same macOS host as the editor measurements:

```text
$ ./build/liquify-review-bench modules/liquid build/review-measure-repository-9-a
review: liquid_files=50 diff_lines=9057 open_to_first_hunk_ms=11.377 comments=200 anchor_mapping_ms=3.95042 moved=200 approximate=0 deleted=0
```

The first number includes diff computation, blob hydration, and construction of every visible
text row; the first hunk can be submitted to the retained layer once that call returns. The anchor
number is the complete batch returned by `repository::map_anchors`.

## Structural editing measurement

`liquify-structural-edit-bench` copies selected files from liquid's own source tree into a fresh
libliquid repository. It chooses the most frequent identifier with at least 50 references, measures
rename preview plus node application, finds a statement node spanning at least 30 lines, and
measures extract preview plus application. Build benchmarks with `-DLIQUIFY_BUILD_BENCH=ON`.

Measured on 2026-09-06 on the same machine as the editor measurements above. The load average was
read immediately after the benchmark:

```text
$ ./build/liquify-structural-edit-bench modules/liquid build/structural-measure-repository-5
structural edit: rename_symbol=unexpected references=667 applied=667 rename_ms=23684.745 extract_lines=36 extract_edits=11 extract_ms=123.852
$ sysctl -n vm.loadavg
{ 92.44 87.23 82.73 }
```

The reported duration is end to end through graph reference discovery, fingerprint validation,
incremental buffer reparses and one repository drip for each changed file.

## Verify

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/liquify version
./build/liquify --smoke
```
