# macOS packaging inputs

`package.sh` stages `Liquify.app`, follows all Mach-O dependencies, copies non-system dynamic
libraries into `Contents/Frameworks`, rewrites their install names to `@rpath`, and records the
unsigned `pkg` and `dmg` digests. It signs nested libraries before applying the hardened runtime to
the app with `entitlements.plist`, creates the final installers, and performs signing,
notarization, stapling, and Gatekeeper assessment when the required identities and App Store
Connect key are available. `Info.plist` defines the application bundle.

`verify_bundle.sh` scans every bundled Mach-O and rejects absolute dependencies or run paths
outside `/usr/lib` and `/System/Library`. It also checks that bundle-relative dependencies exist
and that the executable has its Frameworks run path. `test_bundle_launch.sh` copies an app to a
fresh temporary directory, clears dyld overrides, and runs `liquify version`. Packaging uses both
scripts on the staged app and again after mounting the finished disk image.

The ad hoc no-secret CI path uses `entitlements-ci.plist` so libraries without a Developer Team
identifier can load during the mounted-artifact test. A Developer ID release uses only the empty
production `entitlements.plist` and retains hardened library validation.

The CI workflow imports certificates from secrets into a temporary keychain and removes that
keychain after packaging. No credential is stored in this directory.

The complete dependency rule and manual inspection commands are in
[`docs/packaging/macos.md`](../../docs/packaging/macos.md).
