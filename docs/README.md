# liquify — internal documentation

One folder per topic, one file per subject; every change lands its doc here in the same change
(bridge `CLAUDE.md`). The product specification is `oramasearch/liquid`
`docs/features/21-liquify-ide.md`.

| Topic | Files |
|---|---|
| [`app-shell/`](app-shell/) | [`README.md`](app-shell/README.md) — the window/UI shell (SDL3 + Dear ImGui), the one-TU rule, the smoke self-test, how liquify links libliquid and bumps its pin |
| [`build/`](build/) | [`ci.md`](build/ci.md) — clean-clone CI on Linux, macOS, and Windows, including compilers, dependency caching, tests, and display-backed smoke runs |
