# Flatpak packaging inputs

`com.oramasearch.Liquify.yml` is the maintained second Linux format. CI places the release binary
beside the manifest and runs `flatpak-builder --build-only` when the Freedesktop 26.08 platform and
SDK can be installed. The temporary binary and build directory are not release artifacts.
