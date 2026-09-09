# UI screenshot tool

`screenshot.sh` runs the built `liquify --screenshot` command for every registered light and dark
state, including the editor view, first-run recovery states, settings and accessibility proof, and
writes the review set under `docs/ui/screenshots/`. The registry, output contract,
review procedure and `LIQUIFY_BINARY` override are documented in
[`docs/ui/README.md`](../../docs/ui/README.md).
It covers thirteen states in both themes, including the fuzzy command palette with recent commands and
resolved bindings.

`check-properties.py --build build` verifies the native and Diagnostics screenshot properties
against a baseline, two renderer failures and a moved toolbar. It restores source and rebuilds
before returning. See [the reporting and mutation contract](../../docs/ui/property-tests.md).
