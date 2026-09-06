# Linux packaging inputs

`package.sh` assembles the release executable, adjacent tree-sitter frontend, desktop entry, icon, and `AppRun` launcher into an
AppDir. CI downloads `linuxdeploy` at the digest recorded in the workflow; it collects non-system
runtime libraries and creates the AppImage. The script runs `liquify version` from that artifact
and records its SHA-256 digest.

The AppImage is the published Linux artifact. The Flatpak manifest is maintained separately as a
second format.
