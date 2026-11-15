# Editor view

The editor view combines the retained text surface with liquid's repository, syntax and operation
APIs. `EditorBuffer` and `editor_model` are plain C++ and have no SDL or Dear ImGui dependency;
`src/app.cpp` owns input and rendering. C and C++ syntax comes from the pinned
`liquid-frontend-tree-sitter` module and is converted from public `liquid::syntax_tree` nodes into
retained byte spans and fold ranges.

## Editing and navigation

Positions use zero-based source lines and UTF-8 byte boundaries. A buffer owns one primary caret
and any number of secondary carets. Alt-click adds a caret, primary-modifier+Alt+Up/Down adds one
on the adjacent line, and a normal move clears secondary carets. Typing, Backspace and Delete
apply every caret or selection in one buffer revision. Alt+Shift drag creates a rectangular
selection. Double-click selects a word, primary-modifier+L selects a line, Shift extends a
character selection, and select-all retains its standard binding.

The buffer reads `.editorconfig` through `liquid::repository`. Matching sections set tabs versus
spaces, indentation width and soft wrapping. Tab inserts the configured indentation. The status
bar reports the caret count and active indentation/wrap policy. `visual_rows` is the shared,
deterministic model for soft-wrapped and folded source rows.

The gutter renders one-based line numbers, analysis severity markers, per-line human or agent
attribution, active repository presence, syntax fold markers and a liquid Drive-state badge.
Bracket pairs are found with nesting awareness and outlined when the caret is on either side of a
bracket. Syntax roles map only to Orama design tokens: actions for keywords, AI for types, info for
functions, success for strings, warning for numbers, tertiary text for comments and danger for
preprocessor forms. Both themes therefore come from the same generated palette.

## Search and history

Primary-modifier+F opens literal or regular-expression find and replace for the current buffer.
Primary-modifier+Shift+F opens repository search. Repository queries use
`liquid::repository::search`; results retain path, line, column and snippet and open at their source
location from the Search results panel. Repository replacement searches first, rewrites matching
blobs through `repository::write`, and reports the replacement count.

Primary-modifier+Z and primary-modifier+Shift+Z undo and redo the active buffer's local edits,
including unsaved typing. Switching files and saving keep this history. Liquid's repository
operation history remains a graph API; its structural operations retain their grouped semantics.
The lifetime, native close confirmation and crash policy are in [`buffers.md`](buffers.md).

## Asynchronous data path

`EditorGraph::request_editor_view` parses the volatile source with the loaded syntax frontend,
then reads attribution and presence from the repository and path status from Drive on its worker.
It publishes immutable syntax, folding, gutter and collaboration snapshots. Search, replacement
and operation-history requests use the same serialized worker; the render thread never blocks on
a liquid coroutine.

The tree-sitter frontend is built from liquid's pinned C and C++ grammars and copied to the
binary's adjacent `frontends` directory. The compiled path is also retained as a development-tree
fallback. A module load failure remains visible as editor status while the plain text buffer stays
usable.

## Deterministic proof

`editor-view-light` and `editor-view-dark` seed the same source, two carets, buffer find controls,
repository results, line-number gutter and syntax colors. They read no repository, network or
clock state. The committed images are
[`editor-view-light.png`](screenshots/editor-view-light.png) and
[`editor-view-dark.png`](screenshots/editor-view-dark.png).

`test_editor_model.cpp` covers repository preferences, character/word/line/column selections,
literal and regular-expression replacement, bracket matching, token classification, wrapping and
folding. Buffer tests cover a three-caret edit as one revision. Editor-graph tests exercise the
real dynamic syntax frontend, attribution, repository search/replace and operation-log undo.

Measured on 2026-09-06 on the macOS host after rebasing onto `origin/main`. Both release builds
used the same 50,000-line retained buffer and 30-frame run:

```text
static: smoke: lines=50000 frames=30 visible_lines=43 avg_frame_ms=2.275 p95_frame_ms=2.445 achieved_hz=439.5 display_hz=60.0 avg_typing_ms=2.426 p95_typing_ms=2.457
shared: smoke: lines=50000 frames=30 visible_lines=43 avg_frame_ms=2.385 p95_frame_ms=2.680 achieved_hz=419.3 display_hz=60.0 avg_typing_ms=2.521 p95_typing_ms=2.683
```

Linux proof is deferred: it has not been run yet.
