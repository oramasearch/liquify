# Self-contained macOS bundles

`Liquify.app` must launch on a compatible macOS machine without Homebrew or another package
manager. This rule covers the executable and every Mach-O copied into the bundle, including
dependencies reached through libliquid and its submodules.

## Dependency rule

An absolute dynamic-library reference is allowed only when it starts with `/usr/lib/` or
`/System/Library/`. Every other dynamic library is copied into `Contents/Frameworks`, receives an
`@rpath/<name>` install ID, and has its consumers rewritten to that ID. The executable carries an
`LC_RPATH` value of `@executable_path/../Frameworks`.

`packaging/macos/package.sh` follows each load command recursively. It resolves absolute,
`@rpath`, `@loader_path`, and `@executable_path` source references, rejects unresolved libraries
and filename collisions, removes source run paths, and rewrites the staged copies. This includes
OpenSSL's `libssl` and `libcrypto` when CMake found a dynamic OpenSSL installation. The configured
signing identity is then applied to each nested library before the application bundle is signed;
the no-secret path uses ad hoc signatures.

`packaging/macos/verify_bundle.sh` scans every Mach-O under `Contents/MacOS` and
`Contents/Frameworks`. It fails on an absolute non-system dependency or run path, a relative load
command that dyld cannot resolve, a missing bundle-relative dependency, or a missing application
Frameworks run path. Packaging runs this audit before creating unsigned artifacts, after signing
the staged app, and against the app mounted from the final dmg.

`packaging/macos/test_bundle_launch.sh` uses `ditto` to copy an input app to a fresh temporary
directory, clears dyld override variables, restricts `PATH` to system directories, and runs:

```sh
Liquify.app/Contents/MacOS/liquify version
```

Packaging runs the same relocation test on the signed staged app and on the app in the mounted
dmg. The latter is copied away from `/Volumes` before launch, so both checks exercise a relocated
bundle rather than the build tree.

## Inspect a bundle

Run the enforced audit and relocation test directly:

```sh
packaging/macos/verify_bundle.sh /path/to/Liquify.app
packaging/macos/test_bundle_launch.sh /path/to/Liquify.app
```

Inspect the executable's load commands and Frameworks run path:

```sh
otool -L /path/to/Liquify.app/Contents/MacOS/liquify
otool -l /path/to/Liquify.app/Contents/MacOS/liquify |
  awk '/cmd LC_RPATH/{getline; getline; print}'
```

The first command may show only `/usr/lib/...`, `/System/Library/...`, `@rpath/...`, or
`@executable_path/...` entries. Inspect every bundled library and verify signatures as well:

```sh
find /path/to/Liquify.app/Contents/Frameworks -type f -exec otool -L {} \;
codesign --verify --deep --strict --verbose=2 /path/to/Liquify.app
```

The packaging command creates the pkg and dmg, mounts the dmg read-only, runs these checks, and
detaches it:

```sh
packaging/macos/package.sh build/liquify dist
```
