# UI screenshot tool

`screenshot.sh` runs the built `liquify --screenshot` command for every registered light and dark
state and writes the review set under `docs/ui/screenshots/`. The registry, output contract,
review procedure and `LIQUIFY_BINARY` override are documented in
[`docs/ui/README.md`](../../docs/ui/README.md).
It covers ten states in both themes, including the fuzzy command palette with recent commands and
resolved bindings.
