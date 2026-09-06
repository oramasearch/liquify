# macOS packaging inputs

`package.sh` stages `Liquify.app`, copies non-system dynamic libraries into `Contents/Frameworks`,
rewrites their install names to bundle-relative paths, and records the unsigned `pkg` and `dmg`
digests. It signs nested libraries before applying the hardened runtime to the app with
`entitlements.plist`, creates the final installers, and performs signing, notarization, stapling,
and Gatekeeper assessment when the required identities and App Store Connect key are available.
`Info.plist` defines the application bundle.

Every path mounts the finished disk image, verifies the nested signatures and absence of Homebrew
or `/usr/local` load paths, and runs `liquify version` from the mounted app before publishing it.

The ad hoc no-secret CI path uses `entitlements-ci.plist` so libraries without a Developer Team
identifier can load during the mounted-artifact test. A Developer ID release uses only the empty
production `entitlements.plist` and retains hardened library validation.

The CI workflow imports certificates from secrets into a temporary keychain and removes that
keychain after packaging. No credential is stored in this directory.
