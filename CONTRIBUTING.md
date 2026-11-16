# Contributing to liquify

Thank you for helping. This document says how to build and test liquify, how a change gets in,
and the one thing every contributor does first: sign the Contributor License
Agreement.

## Before you start

- For a bug, open an issue with what you did, what you expected and what happened, and the
  version or commit.
- For a larger change (a new feature, a new dependency, a change to an interface), open an issue
  first so the approach can be agreed before you write it.
- **Security issues are never reported in a public issue.** See [SECURITY.md](SECURITY.md): write
  to **security@orama.com**.

## Build and test

You need CMake 3.28 or newer, Ninja, Clang 17 or newer (Homebrew LLVM on macOS, `clang++-NN` on
Linux), OpenSSL 3, zlib and Node. On macOS: `brew install llvm cmake ninja node openssl@3`. Linux
also needs pkg-config and the FUSE3 development files for liquid.

liquify builds against a sibling checkout of [liquid](https://github.com/oramasearch/liquid) that
is not a submodule (`LIQUIFY_LIQUID_SOURCE_DIR`, default `../liquid`). SDL3, Dear ImGui and
GoogleTest are fetched by CMake on the first configure.

```sh
git clone --recurse-submodules https://github.com/oramasearch/liquid.git
git clone --recurse-submodules https://github.com/oramasearch/liquify.git
cd liquify
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/liquify version
./build/liquify --smoke 30
```

In an existing checkout, run `git submodule update --init --recursive` first. `version` and the
unit tests run without a display; `--smoke` and the native window tests need a desktop session.
More detail is in [docs/build/local.md](docs/build/local.md). A crash in a test (segmentation
fault, bus error, abort) is investigated with an AddressSanitizer build, not rerun until it passes.

A change to the user interface attaches before and after screenshots made with
`tools/ui/screenshot.sh`.

## Making a change

1. Fork the repository and branch from `dev`. `main` is the last release; it moves only when a
   release is cut.
2. Keep one change per pull request, with the tests that prove it. A change in behavior updates
   the documentation it touches in the same pull request.
3. Write commit messages in the [Conventional Commits](https://www.conventionalcommits.org/) form,
   for example `fix(editor): keep the selection on undo`.
4. Sign the Contributor License Agreement before your first pull request is merged (next section).
5. Open the pull request against `dev`. Start its description with what changes, why, and how.

A maintainer reviews it. Pull requests are merged with a merge commit, so every commit you make
stays in the history.

## Contributor License Agreement

liquify is owned by OramaSearch, Inc. Every contributor signs the
[Contributor License Agreement](CLA.md) (CLA) once, before their first pull request is merged. Under
it you assign the copyright in your contributions to OramaSearch, Inc., you keep a licence to use
your own contributions for any purpose, and you grant a patent licence for them. The project is
published under the MIT License (see [LICENSE](LICENSE)).

When you open your first pull request, a bot comments on it with a link to the agreement. Sign
there with your GitHub account; it covers all your later contributions to projects of OramaSearch,
Inc. If you contribute as part of your job, your employer signs too: write to
**opensource@orama.com** for the entity agreement.

## License headers

Every source file starts with the line `SPDX-License-Identifier: MIT`, in that file's comment
syntax, for example `// SPDX-License-Identifier: MIT`. Add it to a new file yourself, or run:

```sh
python3 tools/spdx-headers.py           # adds the line where it is missing
python3 tools/spdx-headers.py --check   # lists files without it
```

Code copied from another project keeps its own license header and is listed in
[THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES). Say so in the pull request, with where it came from
and its license; it must be compatible with the MIT License.

## Code of conduct

Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md). Report unacceptable
behavior to **conduct@orama.com**.
