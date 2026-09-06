# liquify — internal documentation

One folder per topic, one file per subject; every change lands its doc here in the same change
(bridge `CLAUDE.md`). `README.md` at the root is the front door and `CLAUDE.md` the agent
instructions; both point here. The product specification is `oramasearch/liquid`
`docs/features/21-liquify-ide.md`.

| Topic | Files |
|---|---|
| [`design/`](design/) | [`overview.md`](design/overview.md) — what liquify is, why a native editor, what it is built on, where it sits; [`status.md`](design/status.md) — what the source tree does today, by area, with each area's page |
| [`app-shell/`](app-shell/) | [`README.md`](app-shell/README.md) — the window/UI shell (SDL3 + Dear ImGui), the one-TU rule, the CLI, the smoke self-test, how liquify links libliquid and bumps its pin |
| [`build/`](build/) | [`local.md`](build/local.md) — requirements, the native build, the test suite, every command the binary accepts, the full local proof; [`layout.md`](build/layout.md) — the source tree; [`ci.md`](build/ci.md) — clean-clone CI on Linux, macOS, and Windows, including compilers, dependency caching, tests, and display-backed smoke runs |
| [`editor/`](editor/) | [`README.md`](editor/README.md) — the buffer, retained GPU text layer, input modes, save-to-drip path and measurements; [`build.md`](editor/build.md) — graph build, test and deploy panels, event states and bindings; [`interactive-analysis.md`](editor/interactive-analysis.md) — incremental open-buffer parsing and registered graph queries; [`structural-editing.md`](editor/structural-editing.md) — syntax selection, node-level edit records, rename, extract function and the agent surface; [`ai.md`](editor/ai.md) — optional assistant, offline guarantee, graph context, provider, and accepted edits; [`review.md`](editor/review.md) — lake review panel, inline text diff, cursor comments, anchor mapping, and approval controls |
| [`packaging/`](packaging/) | [`README.md`](packaging/README.md) — release artifacts, signing and notarization credentials, verification, digest records, and the later liquid projection switch; [`macos.md`](packaging/macos.md) — self-contained bundle dependency rules, auditing, relocation tests, and inspection |
| [`plugins/`](plugins/) | [`README.md`](plugins/README.md) — the shared native ABI, configured and static loading, graph queries, palette commands, CBOR panel models, fault handling, versioning, and sample plugin |
| [`stub-trees/`](stub-trees/) | [`README.md`](stub-trees/README.md) — graph-backed C/C++ declaration stubs, resolver, parser pins, verification, and liquid-tree benchmark |
