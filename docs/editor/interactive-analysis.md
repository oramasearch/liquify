# Interactive analysis

## Graph queries

`liquify::InteractiveAnalysis` registers seven versioned query kinds with liquid's public query
registry:

| Query kind | Result |
|---|---|
| `liquify.node-at-position/1` | deepest named error-tolerant syntax node, source range and leading trivia |
| `liquify.enclosing-definition/1` | containing function, type, enum or namespace definition |
| `liquify.go-to-definition/1` | compiler-symbol definition when supplied, then an open-buffer declaration, then a closed-file stub |
| `liquify.find-references/1` | compiler references when supplied, otherwise matching identifiers from open trees plus the stub definition |
| `liquify.hover/1` | normalized signature and leading documentation from the resolved declaration stub |
| `liquify.completion/1` | visible open-buffer declarations, compiler symbols and closed-file stub names ordered by source and scope proximity |
| `liquify.diagnostics/1` | the build-diagnostic value for the translation unit associated with the buffer |

Native plugin query names are added to this registry before the graph opens. Their argument
encoder requires deterministic CBOR and their compute function calls the plugin operation with the
frontend ABI ownership and callback conventions. `InteractiveAnalysis::plugin_query` demands the
registered key through the same graph as the built-in queries, so persistence, fingerprints,
cancellation, and error values follow the graph contract rather than a side registry.

Cursor arguments are a normalized path plus UTF-8 byte position. Each cursor query reads the
buffer's volatile overlay. Cross-file queries also read an encoded low-durability stub/symbol input
and a volatile open-buffer revision. Diagnostics read the overlay, the buffer-to-translation-unit
input and that translation unit's diagnostic input. The resulting graph dependencies make an edit
invalidate exactly the interactive work that observed the overlay.

## Open-buffer tree lifecycle

Only open buffers own a `TSParser` and full `TSTree`. `open_buffer` parses once and publishes the
source as `graph_input_kind::overlay_buffer`. `apply_edit` validates the byte range, calls
`ts_tree_edit` on a copy of the previous tree and passes that edited tree back to
`ts_parser_parse_string`. The replacement tree and immutable query snapshot become visible
together under the buffer lock. `close_buffer` clears the graph overlay and destroys the parser,
tree and snapshot.

`IncrementalParseReport` exposes the exact edit, tree-sitter's changed ranges, leaf ranges inside
those changes, and the count of leaves outside them. The locality test changes one numeric token
in a valid three-declaration buffer and proves that every affected leaf intersects a reported
tree-sitter range while the leaves outside it are reused. The invalid fixture separately proves
that all cursor queries continue to answer through error recovery.

## Cancellation and fallback data

Every public demand records its active query key before entering liquid. At edit start the service
invalidates those active keys, which covers a query's first execution before liquid has recorded
its input dependencies. Once a query has completed, liquid's recorded reverse dependencies cancel
later executions through the overlay input. Each compute function reads the overlay again before
returning, so a stopped computation is discarded and cannot publish a stale result.

Closed files enter through `add_stub`; no full parse tree is retained for them. Stub version 2 adds
the leading documentation comment used by hover. `set_symbol_records` accepts the frontend
module's compiler-derived locations when they exist and definition/reference queries prefer them.
This keeps the tree-sitter fallback usable while liquid #66 supplies the production symbol record.

`set_build_diagnostics` records the buffer's translation unit and writes that build diagnostic
list as a graph input. The diagnostics query reads and decodes that value; it does not parse a
second diagnostics format or retain a parallel compiler result.

## Editor runtime and daemon

`liquify::EditorGraph` owns the application-facing lifetime. Its worker starts
`liquid::daemon::start` with the checkout; `errc::already_exists` switches to
`liquid::daemon::attach`, so the first liquify process hosts capture and later clients attach to
the same endpoint. Windows retains interactive analysis while reporting liquid's documented
missing named-pipe transport. A hosted daemon is shut down with the editor; an attached daemon is
left to its owner.

The daemon owns working-tree capture, its persisted graph and the `liquid status` data. The
registered editor query kinds remain in the in-process `InteractiveAnalysis` graph; both stay inside
libliquid and no query crosses the daemon socket. The daemon's public construction API does not
accept an additional query registry.

**The analysis cache is the user's, never the repository's** (liquify#79). The graph
(`liquify-graph`) and the stub graph (`liquify-stub-graph`) live in
`analysis_cache_directory(repository)`: `analysis/<16 hex>` beside liquify's other per-user state
(`$LIQUID_HOME/liquify`, `%LOCALAPPDATA%\liquify`, or `~/.liquid/liquify`), named by a stable hash of
the repository's canonical path. A repository therefore cannot ship a cache the editor reads on open;
caches left in a repository's `.liquid/` by an earlier version are no longer read. Reading a cache is
also bounded: every count is capped by the bytes left before anything is reserved, and declaration
kinds and diagnostic severities are checked before they are cast.

Buffer open, close and edit operations are serialized on the worker. Cursor and diagnostic
demands run concurrently with that worker, allowing the next edit to invalidate an active query
instead of waiting for it. Results carry monotonically increasing request generations; a late
answer cannot replace a newer hover, completion, definition or diagnostic result. The render
thread copies one `EditorGraphSnapshot` containing those answers, graph statistics and daemon
status. The normal reading path exposes only actionable graph state. Repository drip counters and
the measured edit-to-diagnostic duration are available through Diagnostics details. Each concurrent demand runs on
its own thread, which is joined, never detached (liquify#80): the worker joins finished ones as new
work arrives, and `EditorGraph`'s destructor joins every remaining one after the worker has stopped,
so no task can outlive the graph it uses.

`publish_build_diagnostics` is the producer boundary for the build path: it writes the
translation-unit diagnostic input before requesting the editor diagnostic query. Inline build
launch remains liquify #6; the editor runtime does not introduce another compiler or diagnostics
parser.

## Measurement and verification

`liquify-interactive-analysis-bench` extracts stubs for liquid's C/C++ files, keeps only
`src/graph.cpp` open, and measures 200 definition and completion demands at identifiers found in
that file. On the Apple M3 Max macOS host on 2026-09-04 with the pinned liquid tree:

```text
interactive analysis: files=252 open_buffers=1 samples=200 go_to_definition_p95_ms=125.561 completion_p95_ms=194.551
```

Run the focused proof with a new state directory under the build tree:

```sh
cmake -S . -B build -G Ninja -DLIQUIFY_BUILD_BENCH=ON
cmake --build build
ctest --test-dir build --output-on-failure -R InteractiveAnalysis
./build/liquify-interactive-analysis-bench ../liquid build/interactive-analysis-state
```

`EditorGraph.*` additionally proves first-client daemon hosting, attachment to an existing daemon,
overlay edit publication, all editor-facing query results, status backlog formatting and
edit-to-diagnostic measurement.
