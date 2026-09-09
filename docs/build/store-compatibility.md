# CLI, IDE, and store compatibility

liquify pins libliquid at `7ff6ed82f93b916ef206c5776e2761b19536d8f3` (liquid #283,
bounded near-duplicate chunk compression). The previous pin, `fd7ee06ee9f493b2e37e107fe20771fc3853fcbc`,
read object-map versions 1–4. The supported CLI writes version 5. The new pin reads versions 1–5
and owns all store, pack, index-manifest, and repository migration behavior. liquify still builds
it with `LIQUID_BUILD_CLI=OFF`; no liquid source is edited through the submodule.

## Supported artifacts

Until release numbers identify compatible artifacts, use exact source revisions:

| Artifact | Supported revision |
|---|---|
| liquid CLI, built separately | `7ff6ed82f93b916ef206c5776e2761b19536d8f3` |
| libliquid linked into liquify | `7ff6ed82f93b916ef206c5776e2761b19536d8f3` |
| lake used for the native clone proof | `620c249c3e03c455b1392d4691488e7639d01ff6` |

The lake check uses an existing native lake HTTP artifact as an external service. It is not a
source, build, or runtime dependency of liquify. A fresh repository is populated through the
independent CLI's `init`, `cut`, and `push`, then fetched through `liquid clone`. The fixture's
prepopulated protected branch is unrelated to this test repository. Its control plane is in
memory, while repository bytes use the actual lake/libliquid storage implementation.

`liquify version` and `--command help.version` report the IDE revision, libliquid revision, store
marker version 1, and supported object-map versions. The IDE's version string also carries its
revision. Both numerical release versions remain `0.0.1`; upstream liquid's standalone version
output still omits its revision, so record the CLI source revision and build provenance alongside
the executable. The compatibility runner prints both paths and the source revision.

## Unsupported formats and corruption

Repository and editor errors share `repository_error_message`. After libliquid refuses an object
map, the diagnostic reads only its complete 12-byte header. A recognized `LQOBJ` tag with an
unsupported version produces an error naming the encountered version, both build revisions, the
supported range, and the upgrade/export options. It neither parses records nor retries or rewrites
the store. Missing, truncated, malformed, and supported-version corrupt maps retain the original
libliquid error. An unsupported top-level store marker also adds upgrade guidance.

Stores do not record the writer's artifact revision. The message says so rather than inventing
one. Keep the original repository, upgrade the CLI and IDE together, or use a compatible CLI to
export or clone into a separate directory. Never repair compatibility by deleting object metadata.

## Independent integration check

`liquify-independent-cli-compat` is registered in the normal CTest suite. By default its Python
runner clones liquid into the build directory at the supported revision, initializes recursive
submodules, configures a standalone CMake project with `LIQUID_BUILD_CLI=ON`, and builds the CLI
with at most six jobs. This source tree and its objects are separate from the IDE's pinned
library build. Its first run needs GitHub access; later runs reuse that independent checkout.

An already independently built CLI can be supplied with
`-DLIQUIFY_COMPAT_CLI=/absolute/path/to/liquid/build/liquid`. Its adjacent CMake cache must name a
standalone liquid project with CLI enabled, separate from the shared `../liquid` checkout, and its source checkout must
be at the supported revision. The caller is responsible for supplying a current binary from that
build. No CLI is ever enabled in the IDE's CMake project.

The test creates `notes.txt` using the CLI, verifies `liquify --print-tree`, and runs
`liquify-compat-editor`, which uses the actual `RepositoryView` and `EditorBuffer` open, edit,
save, and reopen path. It waits for the view's navigation worker before opening the editor so
larger cloned index caches complete recovery before the second repository handle opens. It checks
the saved drip and cleared dirty flag. The independent CLI then
cuts and exports the repository to Git, where `git show` verifies the IDE's saved bytes. Finally
an isolated copy with object-map version 6 must fail with upgrade guidance and unchanged metadata.
This model-level check supplements the native application edit/save proof; it does not claim to
test keyboard input or rendering.

```sh
cmake -S . -B build -G Ninja -DLIQUIFY_COMPAT_CLI=/absolute/path/to/liquid/build/liquid
cmake --build build -j 6
ctest --test-dir build -R liquify-independent-cli-compat --output-on-failure
```

For an independently cloned lake repository, the same runner accepts
`--lake-repository PATH --lake-file README.md` and applies the same open/edit/save/read-back
checks. Use a disposable clone because the check writes a file and creates a change.

Every future pin bump must rerun the independent CLI check and native static/shared suites,
update this table and the README, and verify any new supported format before expanding the error
classifier's range. After changes to submodule pins, headers, or CMake inputs arrive through a
rebase, delete the build directories and configure again before accepting proof results.

On macOS, the Unix-domain daemon socket path includes `TMPDIR`. A deep absolute temporary
directory can exceed the platform socket-path limit, causing daemon tests to report
`daemon socket path is too long`. Use the system temporary directory for ordinary native test
runs. A short temporary directory is useful only when diagnosing that host limitation; its
relative paths can change test diagnostics. Under concurrent FSEvents load, the daemon status
tests can also intermittently report that the daemon is not running or hosted; liquid#282 tracks
that host condition. Confirm an isolated low-load rerun before treating it as a product failure.
