# Release packaging

Liquify packages the existing release executable directly until liquid issues
[#76](https://github.com/oramasearch/liquid/issues/76) and
[#77](https://github.com/oramasearch/liquid/issues/77) provide platform projection and signing.
The scripts under [`packaging/`](../../packaging/) are shared by pull-request CI and the tag release
workflow, so a published installer has already followed the same build and verification path.
The macOS dependency policy, bundle audit, and relocation test are documented in
[`macos.md`](macos.md).

## Produced artifacts

| Platform | Release artifacts | CI verification |
|---|---|---|
| macOS | `liquify-0.0.1-macos.pkg`, `liquify-0.0.1-macos.dmg` | transitive Mach-O dependency audit, relocated launch before packaging and from the mounted dmg, hardened-runtime `codesign`, notarization and stapling when credentials are present, then `spctl` assessment |
| Windows | `liquify-0.0.1-windows-x64.msix` | bundled non-system DLLs, SHA-256 Authenticode signature with RFC 3161 timestamp, `signtool verify`, `Add-AppxPackage`, installed `liquify version` with a system-only `PATH` |
| Linux | `liquify-0.0.1-x86_64.AppImage` | `liquify version` on Ubuntu 24.04 and Fedora 42 |

The AppImage is the published Linux format. The checked-in
[`com.oramasearch.Liquify.yml`](../../packaging/flatpak/com.oramasearch.Liquify.yml)
is the maintained second format. CI installs the Freedesktop 26.08 platform and SDK and runs
`flatpak-builder --build-only`; if that runtime cannot be installed, the workflow summary records
the validation as skipped instead of claiming it ran.

Each platform artifact includes a SHA-256 file. macOS and Windows also include an
`unsigned.sha256` record captured immediately before platform signing. The release workflow adds
`SHA256SUMS` over every published installer and platform digest record. The unsigned and signed
records use the same final filename so the pre-signing and post-signing bytes can be related
without publishing an unsigned installer.

## Credentials

Steve owns and rotates the release credentials. GitHub Actions receives them only from repository
secrets; no certificate, private key, password, or API credential belongs in the repository.

macOS uses these secrets:

| Secret | Purpose |
|---|---|
| `LIQUIFY_APPLE_CERTIFICATE_P12` | base64 PKCS#12 containing the Developer ID Application and Installer certificates |
| `LIQUIFY_APPLE_CERTIFICATE_PASSWORD` | PKCS#12 import password |
| `LIQUIFY_APPLE_APPLICATION_IDENTITY` | exact Developer ID Application identity passed to `codesign` |
| `LIQUIFY_APPLE_INSTALLER_IDENTITY` | exact Developer ID Installer identity passed to `productbuild` |
| `LIQUIFY_APPLE_NOTARY_KEY_P8` | base64 App Store Connect API private key |
| `LIQUIFY_APPLE_NOTARY_KEY_ID` | App Store Connect key identifier |
| `LIQUIFY_APPLE_NOTARY_ISSUER_ID` | App Store Connect issuer identifier |

The workflow imports the PKCS#12 into a temporary keychain and decodes the API key under
`RUNNER_TEMP`. With the complete set, both installers are submitted with
`xcrun notarytool submit --wait`, stapled, and required to pass Gatekeeper. Without it, the app and
disk image receive an ad hoc hardened-runtime signature, notarization is explicitly reported as
skipped, and the expected Gatekeeper rejection is recorded without failing pull-request CI.
Every non-system dynamic library is copied transitively into `Liquify.app/Contents/Frameworks`,
rewritten to `@rpath`, audited, and signed before the app, so an installed build never loads a
package-manager or build-tree path.
The ad hoc test app alone disables library validation because ad hoc signatures have no shared
Developer Team identifier. Developer ID releases use the empty production entitlement file and
retain hardened library validation.

Windows uses these secrets:

| Secret | Purpose |
|---|---|
| `LIQUIFY_WINDOWS_CERTIFICATE_PFX` | base64 code-signing PFX |
| `LIQUIFY_WINDOWS_CERTIFICATE_PASSWORD` | PFX password |
| `LIQUIFY_WINDOWS_PUBLISHER` | manifest publisher subject matching the certificate |
| `LIQUIFY_WINDOWS_TIMESTAMP_URL` | optional RFC 3161 timestamp service; defaults to DigiCert |

When the PFX is absent, CI creates a temporary `CN=Liquify CI` code-signing certificate, adds its
public certificate to the runner's trusted-people store, signs the MSIX through the identical
`signtool sign /fd sha256 /tr ... /td sha256` path, installs it, runs the packaged executable, and
removes both the package and certificate. Tag releases use the supplied release certificate.

## Release and verification

Pushing a `v*` tag starts [the release workflow](../../.github/workflows/release.yml). It invokes
the reusable three-platform CI workflow, waits for the Fedora AppImage check, creates
`SHA256SUMS`, verifies that file, and creates the GitHub release with the installers plus their
platform digest records.

After downloading a release, verify its aggregate digests on macOS or Linux:

```sh
sha256sum --check SHA256SUMS
```

Verify the platform trust records with native tools:

```sh
# macOS
pkgutil --check-signature liquify-0.0.1-macos.pkg
spctl --assess --type install --verbose=4 liquify-0.0.1-macos.pkg
spctl --assess --type open --context context:primary-signature \
  --verbose=4 liquify-0.0.1-macos.dmg
xcrun stapler validate liquify-0.0.1-macos.pkg
xcrun stapler validate liquify-0.0.1-macos.dmg

# Linux
APPIMAGE_EXTRACT_AND_RUN=1 ./liquify-0.0.1-x86_64.AppImage version
```

On Windows, use a Developer PowerShell prompt:

```powershell
signtool verify /pa /v .\liquify-0.0.1-windows-x64.msix
Add-AppxPackage .\liquify-0.0.1-windows-x64.msix
$package = Get-AppxPackage OramaSearch.Liquify
& "$($package.InstallLocation)\liquify.exe" version
```

## Projection follow-up

Part B remains deferred while liquid #76 and #77 are open. Once both land, a dedicated follow-up
issue will bump `modules/liquid`, build liquify under `liquid wrap`, replace the platform scripts
with `liquid project macos-pkg|msix|appimage` and `liquid sign macos|windows`, and retain this tag
release workflow. Those projected artifacts will carry liquid provenance. Auto-update, app-store
submission, Snap, and another installer writer remain outside this issue.
