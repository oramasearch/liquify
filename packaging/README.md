# Release packaging sources

Platform inputs and scripts live under `macos/`, `windows/`, `linux/`, and `flatpak/`. They package
the existing CMake release executable; they do not introduce another build or dependency on a
private product. The release and pull-request workflows use these same files.

The operating and credential details are in [`docs/packaging/README.md`](../docs/packaging/README.md).
The macOS scripts recursively bundle third-party dylibs, reject non-system absolute Mach-O load
paths, and prove the signed app launches after being copied to a fresh location. See
[`docs/packaging/macos.md`](../docs/packaging/macos.md) for the enforced rule and inspection
commands.
