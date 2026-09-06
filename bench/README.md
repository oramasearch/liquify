# Stub-tree benchmark

`liquify-stub-tree-bench` measures cold stub extraction for C and C++ source files and resolution
of 1,000 declaration names using only decoded stubs. It takes a source root and a graph-state
directory. Build it with `-DLIQUIFY_BUILD_BENCH=ON`; the recorded liquid-tree run is in
[`docs/stub-trees/README.md`](../docs/stub-trees/README.md).

`liquify-editor-graph-bench` imports liquid's own source tree, measures lazy repository opening,
and verifies that an editor diagnostic is visible while link completion is still held. Its recorded
run is in [`docs/editor/README.md`](../docs/editor/README.md).

`liquify-ai-context-bench` loads liquid's declarations into the interactive graph, finds a
definition with at least 20 resolved references, and compares its budgeted assistant block with a
naive whole-file payload. Its recorded run is in
[`docs/editor/README.md`](../docs/editor/README.md).

`liquify-review-bench` imports 50 files from liquid's tree, builds their inline text-diff rows,
and maps 200 cursor anchors after a rebase-style insertion. `liquify-structural-edit-bench` copies
real liquid implementation files into a fresh repository, then measures an end-to-end rename with
at least 50 references and extraction of a statement range spanning at least 30 lines. Their
recorded runs and the structural-edit benchmark host load average are in
[`docs/editor/README.md`](../docs/editor/README.md).
