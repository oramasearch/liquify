# Structural editing

liquify uses the retained C and C++ tree-sitter trees from `InteractiveAnalysis` for syntax-aware
selection, refactoring and node-level mutations. Every request runs on `EditorGraph`'s existing
worker. The render thread enqueues a request and later consumes a generation-stamped result.

## Selection

Expand selection starts at `node_at_position` and walks the exact parent chain in the retained
tree. Repeated expansion selects the next enclosing syntax node; shrink restores the preceding
range. Definition selection chooses the first enclosing function, class, struct, enum or namespace.

Reference selection starts with the symbol under the cursor and runs `find_references` as a graph
query. Declaration ranges are narrowed to their identifier nodes. Files returned from stubs or
compiler symbol records are read from the repository and opened into the analysis service only when
needed. The references panel lists every range and opens its file when selected.

The keyboard and palette commands are:

| Command | Binding |
|---|---|
| Expand syntax selection | Primary+Shift+Right |
| Shrink syntax selection | Primary+Shift+Left |
| Select enclosing definition | Primary+Shift+D |
| Select symbol references | Primary+Shift+F |
| Rename symbol | Primary+Shift+R |
| Extract function | Primary+Shift+X |

Primary is Command on macOS and Control on the other desktop platforms.

## Edit records

`include/liquify/structural_edit.hpp` defines the node-level contract:

```text
StructuralEdit {
  target: {
    path,
    node: { kind, range, fingerprint }
  },
  operation: replace_node | insert_before | insert_after | delete_node,
  text
}

StructuralEditResult {
  applied: [{ path, range }],
  refused: [{ target, reason }]
}
```

Ranges contain zero-based byte, line and column coordinates. The fingerprint is the BLAKE3 digest
of the node's exact source bytes. Before applying an edit, `StructuralEditor` finds a node with the
same path, kind and range and recomputes the fingerprint. A missing node or changed fingerprint is
reported in `refused`; the editor does not search for a similar node.

Requests and results have deterministic RFC 8949 canonical CBOR encodings. Both envelopes contain
`v: 1`; a request contains `edits`, and a result contains `applied` and `refused`. Unknown versions,
operations, invalid ranges and repository-escaping paths are rejected. The headless entry point is:

```sh
liquify --apply-edits request.cbor /path/to/repository > result.cbor
```

Accepted edits are ordered from the end of each file toward the beginning. Overlapping edits are
refused. All accepted mutations for a file pass through `EditorBuffer`, followed by one
`liquid::repository::write`; therefore each changed file creates one drip regardless of its edit
count.

## Rename

Rename turns every code reference returned by `find_references` into a `replace_node` edit for its
exact identifier node. Open buffers, repository files reached through stubs and compiler symbol
records all use the same path. Whole-word occurrences inside comments, strings and character
literals are excluded from the edits and listed as unresolved for review.

The preview groups edits by file. All files start accepted; the user can clear individual files,
apply the selected files, apply all files or dismiss the preview. This is a syntax-and-graph rename,
not a Clang semantic rename. Ambiguous graph facts remain visible rather than being inferred.

## Extract function

Extract function accepts a selection inside one C or C++ function and expands it to the complete
top-level statements contained by the selection. It inserts a `static void` definition before the
enclosing definition, replaces the first selected statement with a call and deletes the remaining
selected statements.

Identifiers used inside the selected statements and declared earlier in the containing scope
become parameters. Declarations inside the selected range remain local. Simple declaration types
come from the retained tree-sitter declaration records. Written C++ values use reference
parameters; written C values use pointer parameters and their uses are rewritten through the
pointer. If a type cannot be recovered, the preview contains a diagnostic and no edits. A selection
containing `return`, `break`, `continue` or `goto` is also diagnostic-only because its control flow
cannot be preserved by the generated `void` function.

The implementation deliberately covers C and C++ statement extraction with simple declarators.
Templates, structured bindings, macro-sensitive types, output return values and compiler-driven
signature rewriting are left for a semantic refactoring layer.

## Agent surface and ownership

Bridge decision 30 places the permanent agent edit operation in libliquid so it can be used without
the IDE. Until that liquid companion leaf lands, `StructuralEditor` is a local adapter that mirrors
the intended request and result records. It compiles the pinned liquid canonical CBOR codec into
`liquify-core` so both static and shared libliquid configurations use identical bytes without
depending on hidden library symbols. The editor, tests and headless command all use this one
node-level entry point; there is no whole-file agent mutation path.

The stable plugin ABI is not available yet. `structural_edit.hpp` and `--apply-edits` are the plain
C++ and script surfaces for this phase. The companion liquid leaf must add the same
`structural_edit` operation to the daemon's local endpoint and MCP rendering, with closed-file trees
provided by liquid's frontend fallback parser.
