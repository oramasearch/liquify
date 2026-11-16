# Release packaging sources

Platform inputs and scripts live under `macos/`, `windows/`, `linux/`, and `flatpak/`. They package
the existing CMake release executable; they do not introduce another build or dependency on a
private product. The release and pull-request workflows use these same files.

The operating and credential details are in [`docs/packaging/README.md`](../docs/packaging/README.md).

**A release is never ad hoc or self-signed** (liquify#77). With `LIQUIFY_RELEASE=1`, or when building
a `v*` tag (`GITHUB_REF=refs/tags/v...`), `macos/package.sh` stops before building unless the
Developer ID application and installer identities and the notary key (`LIQUIFY_APPLE_*`) are all set,
and `windows/package.ps1` stops unless `-PfxPath` names a certificate and `-Publisher` is not the test
publisher `CN=Liquify CI`. Ad hoc and self-signed packages remain for local and test builds. The
workflow files are not changed (no CI); whether the release workflow stays is Steve's decision. Linux
packages are not signed by these scripts.
The macOS scripts recursively bundle third-party dylibs, reject non-system absolute Mach-O load
paths, and prove the signed app launches after being copied to a fresh location. See
[`docs/packaging/macos.md`](../docs/packaging/macos.md) for the enforced rule and inspection
commands.
