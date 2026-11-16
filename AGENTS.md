# AGENTS.md

Read `CLAUDE.md` first. It is the instruction file for this repository and applies in full to
every agent. Then `README.md` and `docs/README.md`.

Non-negotiables, restated from `CLAUDE.md`:

- Every change documents itself in `docs/<topic>/*.md` **in the same commit**, and updates the
  status lines in `docs/design/status.md` and `CLAUDE.md` when they stop being true.
- **No AI attribution anywhere.** No `Co-Authored-By` for an assistant, no "Generated with", no
  session or model trailers, in commits or pull requests.
- No time estimates. The word "seam" is banned; name the mechanism instead.
- Work on a feature branch named for the issue, open a pull request against `dev` that references
  the issue, and never push to `main` directly. Contributors sign the CLA (`CLA.md`) once.
- Never take a dependency on Zetta or on lake; liquify talks only to liquid.
- `src/app.cpp` is the only translation unit that includes SDL3 or ImGui.
