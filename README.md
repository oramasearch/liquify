# liquify

> **Status: experimental.** liquify is under active development and not ready for daily work.
> Expect missing features, rough edges, changes to its settings and keymaps without notice, and
> bugs that can lose unsaved work. There are no versioned releases yet. Use it to try liquid's
> editing model, not on code you cannot afford to redo.

**The IDE, built on liquid's incremental graph. Early: no release yet.**

liquify is a native, cross-platform editor for repositories kept in [liquid](https://github.com/oramasearch/liquid).
Everything it knows about files, changes, builds, analysis and search comes from libliquid, so a
save is a signed drip, a build runs through liquid's daemon, and code intelligence comes from the
same graph liquid uses. It is one binary: SDL3 and Dear ImGui for the shell, and a retained GPU text
layer for the buffer.

Part of [Orama Lake](https://github.com/oramasearch/lake-platform), the sovereign AI stack.
MIT-licensed.

## What it does

What is built today, with its limit where there is one:

- **Front door**: open a folder, create a repository, clone one from lake, reopen a recent one,
  your local signing identity, more windows. [Docs](docs/ui/first-run.md)
- **Editor**: GPU text buffers with multi-cursor and column editing, search and replace, folds,
  liquid syntax and per-buffer undo; Save writes a drip and Cut Change names the saved drips as a
  change. [Docs](docs/ui/editor.md), [cut](docs/editor/cut.md)
- **Navigation and C/C++ analysis**: Quick Open, symbol search, definitions, references,
  breadcrumbs and history; diagnostics, completion and structural edits (rename, extract
  function) for C and C++. Quick Open does not yet pass its real-window acceptance scenario.
  [Docs](docs/ui/navigation.md), [analysis](docs/editor/interactive-analysis.md)
- **Lake**: push to the lake a repository came from, list, open and request reviews, comment at
  the cursor and approve. Choosing among several lakes is not built yet. [Docs](docs/editor/review.md)
- **Build and test**: F7 builds and F8 runs tests through liquid's daemon; with no recorded build,
  F7 proposes a command and records it. Recording a build on macOS may need a filesystem extension
  liquid's sandbox uses. [Docs](docs/editor/build.md)
- **Shell**: one panel system (dock, float, tabs), a command palette with one row per action and
  rebindable keys, searchable `.liquid` settings, light and dark themes, accessibility labels,
  high contrast and reduced motion. Keys, the palette and the native macOS menus all run through
  one command registry, so a rebind in settings reaches all three at once.
  [Docs](docs/ui/README.md)
- **Plugins**: native plugins, each run in its own sandboxed host process; a plugin a repository
  names loads only once you trust its digest. Reads are not sandboxed, and Windows has no network
  sandbox. [Docs](docs/plugins/README.md)
- **Optional**: multiplayer presence and attribution, and an AI assistant that is off by default
  and opens no connection while off. Accepted assistant edits are still signed as you, not as the
  assistant. [Docs](docs/multiplayer.md), [assistant](docs/editor/ai.md)

The full list, area by area, is in [the implementation status](docs/design/status.md).

## Quickstart

There is no release binary yet, so liquify is built from source.

### Build from source

On macOS: CMake 3.28 or newer, Ninja, Clang 17 or newer (Homebrew LLVM), Node.js, OpenSSL and zlib
(`brew install llvm cmake ninja node openssl@3`). liquid is not a submodule: clone it next to
liquify, where the build looks for it (`../liquid`), or pass its path with
`-DLIQUIFY_LIQUID_SOURCE_DIR`. SDL3, Dear ImGui and GoogleTest are fetched by CMake.

```sh
git clone --recurse-submodules https://github.com/oramasearch/liquid.git
git clone --recurse-submodules https://github.com/oramasearch/liquify.git
cd liquify
cmake -S . -B build -G Ninja
cmake --build build
./build/liquify
```

You should see the front door: open a folder that holds a liquid repository, or create one. Run
the tests with `ctest --test-dir build --output-on-failure`; the native window tests need a
logged-in desktop. Linux requirements and the rest of the build options are in
[`docs/build/local.md`](docs/build/local.md) and [`docs/build/ci.md`](docs/build/ci.md).

liquid and liquify are not yet paired by version: build both from their default branches.
`liquify version` names the IDE and library revisions, and
[`docs/build/store-compatibility.md`](docs/build/store-compatibility.md) has the store
compatibility rule.

## How it fits the stack

liquify talks only to liquid: libliquid in process for repositories, analysis and edits, and
liquid's daemon for builds and sync. Through liquid it pushes to, pulls from and reviews on a lake.
It never depends on lake or zetta directly.

| Piece | What it does | Relation to liquify |
|---|---|---|
| [liquid](https://github.com/oramasearch/liquid) | Version control, builds, packages, deploys | liquify links libliquid and drives its daemon |
| [lake](https://github.com/oramasearch/lake) | The AI workspace, and where liquid repositories collect | liquify pushes, pulls and reviews on it through liquid |
| [zetta](https://github.com/oramasearch/zetta) | The engine | not used by liquify |
| [orama-operator](https://github.com/oramasearch/orama-operator) | Runs the stack on Kubernetes | not used by liquify |

## Known limits

- No versioned release and no signed installer yet.
- Quick Open and some keyboard paths still fail their real-window acceptance checks, and native
  menus do not follow rebound keys.
- One lake per repository: the lake a repository was cloned from, or one you set.
- Accepted assistant edits are signed as you; agent attribution waits on liquid.
- Plugin sandboxes do not deny reads, and Windows has no network sandbox; a slow plugin can stall
  the window for up to its deadline.
- The Linux and Windows builds are exercised less often than macOS.

## Documentation

[`docs/README.md`](docs/README.md) indexes one folder per topic. Start with
[`docs/design/overview.md`](docs/design/overview.md) (what liquify is),
[`docs/design/status.md`](docs/design/status.md) (what works, by area),
[`docs/ui/design-system.md`](docs/ui/design-system.md) (the look),
[`docs/ui/commands.md`](docs/ui/commands.md) (every command and key, generated from the code) and
[`docs/build/layout.md`](docs/build/layout.md) (the source tree).

## Community

- Bugs and requests: [issues](https://github.com/oramasearch/liquify/issues)
- Security reports: privately, per [SECURITY.md](SECURITY.md). Please don't open a public issue.

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) for how to build, test and open a
pull request (against `dev`). Every contributor signs the
[Contributor License Agreement](CLA.md) once, before their first pull request is merged.
[`CLAUDE.md`](CLAUDE.md) and [`AGENTS.md`](AGENTS.md) carry the repository's rules for people and
agents alike.

Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).

## License

MIT. See [LICENSE](LICENSE) and [COPYRIGHT](COPYRIGHT). Third-party components keep their own
licenses, listed in [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).

"Orama Lake", "liquid", "lake", "liquify" and their logos are trademarks of OramaSearch, Inc.
