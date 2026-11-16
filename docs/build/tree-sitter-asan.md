# tree-sitter under AddressSanitizer

liquify parses repository files with the pinned tree-sitter runtime and its C and C++ grammars
(`modules/tree-sitter*`). The C++ grammar has an external scanner, `src/scanner.c`, which lexes raw
string literals (`R"delim(...)delim"`). Repository input is untrusted, so these are checked under
AddressSanitizer (liquify#92). This page says what the harness does, how to run it, and the result
of each run.

## What the harness does

`tools/tree-sitter-asan/` is a standalone CMake project. It compiles the same sources as liquify's
own `CMakeLists.txt` (`lib/src/lib.c`, `tree-sitter-c/src/parser.c`, `tree-sitter-cpp/src/parser.c`
and `scanner.c`), plus `harness.cpp`, with `-fsanitize=address -fno-omit-frame-pointer
-fsanitize-recover=address`. It is configured into its own build directory, so the sanitizer flags
never reach liquify's build.

Every input is parsed with **both** grammars:

| Input | Source |
|---|---|
| Corpus examples | Every example in `test/corpus/` of both grammars (the C++ grammar's `test/corpus/c/` included), split out of the corpus format |
| Source files | `test/highlight/` and `examples/` of both grammars, each file as one input |
| Generated raw strings | Raw string literals for every prefix (`R`, `u8R`, `uR`, `UR`, `LR`) with delimiters of 0 to 20 characters, around the scanner's 16-character limit: complete, unterminated, mismatched, nested, with spaces, backslashes and newlines in the delimiter, and inside macros |

For each input and grammar it runs:

- a full parse, walking every node with a tree cursor and printing the tree with `ts_node_string`;
- a parse of every prefix and every suffix (an even sample of 512 cut points for larger inputs);
- 32 seeded mutations (overwritten bytes, inserted fragments, deleted and duplicated ranges,
  arbitrary bytes, a cut ending in a fragment), each parsed from scratch **and** reparsed from the
  edited original tree after `ts_tree_edit`, as liquify's editor does, followed by
  `ts_tree_get_changed_ranges`;
- a parse of the same bytes read as UTF-16LE and UTF-16BE (odd lengths, unpaired surrogates).

The fragments target the lexers, the raw string scanner and error recovery: raw string openers and
closers, invalid and truncated UTF-8, NUL, CR and line continuations, comments, preprocessor lines
and C++ punctuation.

A parse error on malformed input is the expected result and is not a finding; the finding is an
AddressSanitizer report. The binary is built with recovery and run with `halt_on_error=0`, so every
report in a run is collected. After each report the harness prints the grammar, the operation and
the input, and writes the input bytes to `reports/report-N.bin` for reproduction.

## How to run it

```sh
git submodule update --init modules/tree-sitter modules/tree-sitter-c modules/tree-sitter-cpp
tools/tree-sitter-asan/run.sh                         # builds into build-tree-sitter-asan/
tools/tree-sitter-asan/run.sh /tmp/ts-asan --mutations 256 --seed 7   # more, other seeds
```

`run.sh` configures and builds the harness, then runs `tree-sitter-asan --self-test` (a deliberate
heap overflow that must produce exactly one report, proving the build is instrumented and reports
are caught), then runs the harness over every corpus, highlight test and example of both grammars.
The log is `<build>/reports/asan.log`; it starts with the three pinned commits. The exit status is
0 when there were no reports and 1 when there were any. The options are `--mutations N`,
`--max-slices N` (cut points per input) and `--seed S`; paths given after them are added to the
inputs.

When the tree-sitter or a grammar pin moves, run it again and add the result below.

**When it reports:** file one issue per distinct report, with the stack from the log, the
operation, the input label and the saved `report-N.bin`, and list it on liquify#92 (or the issue
for the pin move). A report in upstream code is fixed in a private copy of the grammar or runtime
and the submodule pointed there; it is offered upstream only as a separate, deliberate step.

## What it does not cover

- Coverage-guided fuzzing. The mutations are seeded and fixed, not guided by coverage; Apple's
  toolchain ships no libFuzzer.
- UndefinedBehaviorSanitizer and MemorySanitizer.
- liquify's own code that reads the trees (`src/stub_tree.cpp`, `src/interactive_analysis.cpp`);
  only the runtime and the grammars are instrumented here.
- Linux and Windows. The runs so far are on macOS.

## Runs

| Date | Pins (tree-sitter, -c, -cpp) | Host | Inputs | Parses | ASan reports |
|---|---|---|---|---|---|
| 2026-11-15 | `d97971e2` (v0.26.13), `b780e47`, `f41e1a0` | macOS 14.6.1, arm64, AppleClang 15.0.0 | 33 files: 264 corpus examples, 9 source files; 1,155 generated raw strings; 1,428 inputs x 2 grammars | 2,856 full, 342,772 prefix and suffix, 91,392 mutated, 91,392 incremental reparses, 5,712 UTF-16; 82,649,248 nodes walked, 372,463,878 bytes parsed | **0** (self-test: 1 of 1 expected) |

On the 2026-11-15 run, unmodified inputs that parsed with an error node: corpus examples, C
grammar 92 of 264 and C++ grammar 7 of 264 (each grammar also reads the other's corpus, and the
corpora include error-recovery tests); source files, 5 and 3 of 9; generated inputs, all 1,155
under both (most are malformed by design). These show the inputs reached the grammars as
intended; none is a finding.
