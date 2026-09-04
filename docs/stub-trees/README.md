# Stub trees

## What

`liquify::StubTrees` registers `liquify.stub-tree/2` with liquid's public query registry. Its query
argument is the language and BLAKE3 digest of a source blob. The query reads a graph input with that
same key, extracts the source with tree-sitter, and returns a compact deterministic binary record.
The record contains each declaration's kind, name, modifiers, normalized signature, leading
documentation comment, and byte and zero-based line/column range. The liquid graph persists that
value as its record set, keyed by the query argument; equal C or equal C++ blobs therefore share
one persisted stub value. Version 2 has a separate query kind, so persisted version 1 values cannot
be decoded as the new record shape.

The extractor uses the tree-sitter runtime at `v0.26.13`, C grammar at `v0.24.2`, and C++ grammar at
`v0.23.4`, each pinned under `modules/`. It records functions, types, variables, fields, and
namespaces. Tree-sitter is error tolerant, so a syntactically incomplete open buffer still produces
the declarations it can recognize.

`StubResolver` accepts already decoded `StubTree` values and returns matching declaration locations.
It neither reads source files nor constructs parser trees. Closed files can therefore serve
cross-file declaration resolution through persisted stubs alone.

## Why

The Phase 4 design makes a stub an action-like graph output: cross-file resolution needs
declaration identity and ranges, not each closed file's full parse tree. Keying the query by blob
digest makes unchanged content reuse the existing record, while an edit has a different input key
and computes a new record. Language participates in the key because the same bytes can parse
differently as C and C++.

## Verify

```sh
cmake -S . -B build -G Ninja -DLIQUIFY_BUILD_BENCH=ON
cmake --build build
ctest --test-dir build --output-on-failure -R StubTrees
stub_state=$(mktemp -d)
./build/liquify-stub-tree-bench modules/liquid "$stub_state"
```

The tests cover fixture declaration lists, equal-blob sharing, persistence across graph reopen,
cross-file resolution using only stubs, and changed-versus-reused blob behavior.

On the Apple M3 Max macOS host on 2026-09-04, the liquid checkout contained 244 C/C++ files outside
its nested modules and build output:

```text
stub extraction: blobs=244 elapsed_us=3768635 per_blob_us=15445.2
stub resolution: lookups=1000 elapsed_us=7293 per_lookup_us=7.293 resolved=16563
```

## Change

To add a grammar, pin its repository beneath `modules/`, compile its generated parser in
`CMakeLists.txt`, extend `StubLanguage` and `stub_language_for_path`, then add declaration fixtures
and expected lists. A grammar change that alters the record format increments the encoding version
in `src/stub_tree.cpp` and keeps the decoder for already persisted records or migrates them through a
new query kind.
