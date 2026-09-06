# liquify — internal documentation

One folder per topic, one file per subject; every change lands its doc here in the same change
(bridge `CLAUDE.md`). The product specification is `oramasearch/liquid`
`docs/features/21-liquify-ide.md`.

| Topic | Files |
|---|---|
| [`app-shell/`](app-shell/) | [`README.md`](app-shell/README.md) — the window/UI shell (SDL3 + Dear ImGui), the one-TU rule, the smoke self-test, how liquify links libliquid and bumps its pin |
| [`build/`](build/) | [`ci.md`](build/ci.md) — clean-clone CI on Linux, macOS, and Windows, including compilers, dependency caching, tests, and display-backed smoke runs |
| [`editor/`](editor/) | [`README.md`](editor/README.md) — the buffer, retained GPU text layer, input modes, save-to-drip path and measurements; [`interactive-analysis.md`](editor/interactive-analysis.md) — incremental open-buffer parsing and registered graph queries; [`structural-editing.md`](editor/structural-editing.md) — syntax selection, node-level edit records, rename, extract function and the agent surface; [`ai.md`](editor/ai.md) — optional assistant, offline guarantee, graph context, provider, and accepted edits |
| [`stub-trees/`](stub-trees/) | [`README.md`](stub-trees/README.md) — graph-backed C/C++ declaration stubs, resolver, parser pins, verification, and liquid-tree benchmark |
