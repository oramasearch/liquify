#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <liquify-binary> <output-directory>" >&2
    exit 2
fi

binary_path="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
output_directory="$2"
script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
application_identity="${LIQUIFY_APPLE_APPLICATION_IDENTITY:-}"
installer_identity="${LIQUIFY_APPLE_INSTALLER_IDENTITY:-}"
notary_key_path="${LIQUIFY_APPLE_NOTARY_KEY_PATH:-}"
notary_key_id="${LIQUIFY_APPLE_NOTARY_KEY_ID:-}"
notary_issuer_id="${LIQUIFY_APPLE_NOTARY_ISSUER_ID:-}"
version="${LIQUIFY_PACKAGE_VERSION:-0.0.1}"

if [[ ! -x "$binary_path" ]]; then
    echo "liquify binary is not executable: $binary_path" >&2
    exit 1
fi

mkdir -p "$output_directory"
output_directory="$(cd "$output_directory" && pwd)"
work_directory="$(mktemp -d "${TMPDIR:-/tmp}/liquify-package.XXXXXX")"
mounted_path=""

cleanup() {
    if [[ -n "$mounted_path" ]]; then
        hdiutil detach "$mounted_path" >/dev/null 2>&1 || true
    fi
    rm -rf "$work_directory"
}

trap cleanup EXIT

app_path="$work_directory/Liquify.app"
frameworks_path="$app_path/Contents/Frameworks"
unsigned_component="$work_directory/liquify-component-unsigned.pkg"
signed_component="$work_directory/liquify-component.pkg"
unsigned_pkg="$work_directory/liquify-${version}-macos-unsigned.pkg"
unsigned_dmg="$work_directory/liquify-${version}-macos-unsigned.dmg"
final_pkg="$output_directory/liquify-${version}-macos.pkg"
final_dmg="$output_directory/liquify-${version}-macos.dmg"
unsigned_digests="$output_directory/liquify-${version}-macos.unsigned.sha256"

bundle_dependencies() {
    local target="$1"
    local dependency
    local destination

    while IFS= read -r dependency; do
        case "$dependency" in
            /System/* | /usr/lib/*)
                continue
                ;;
            @*)
                echo "unresolved dependency in $target: $dependency" >&2
                exit 1
                ;;
        esac

        if [[ ! -f "$dependency" ]]; then
            echo "dependency does not exist: $dependency" >&2
            exit 1
        fi

        destination="$frameworks_path/$(basename "$dependency")"
        if [[ ! -f "$destination" ]]; then
            cp -L "$dependency" "$destination"
            chmod u+w "$destination"
            bundle_dependencies "$destination"
            install_name_tool -id "@rpath/$(basename "$dependency")" "$destination"
        fi
        install_name_tool \
            -change "$dependency" "@rpath/$(basename "$dependency")" "$target"
    done < <(otool -L "$target" | tail -n +2 | awk '{print $1}')
}

mkdir -p "$app_path/Contents/MacOS" "$frameworks_path"
install -m 755 "$binary_path" "$app_path/Contents/MacOS/liquify"
install -m 644 "$script_directory/Info.plist" "$app_path/Contents/Info.plist"
install_name_tool -add_rpath @executable_path/../Frameworks "$app_path/Contents/MacOS/liquify"
bundle_dependencies "$app_path/Contents/MacOS/liquify"

pkgbuild \
    --component "$app_path" \
    --identifier com.oramasearch.liquify \
    --install-location /Applications \
    --version "$version" \
    "$unsigned_component"
productbuild --package "$unsigned_component" "$unsigned_pkg"
hdiutil create \
    -fs HFS+ \
    -format UDZO \
    -srcfolder "$app_path" \
    -volname Liquify \
    "$unsigned_dmg"

(
    cd "$work_directory"
    shasum -a 256 "$(basename "$unsigned_pkg")" "$(basename "$unsigned_dmg")"
) | sed 's/-unsigned//' > "$unsigned_digests"

if [[ -n "$application_identity" ]]; then
    for library in "$frameworks_path"/*.dylib; do
        codesign --force --options runtime --timestamp --sign "$application_identity" "$library"
    done
    codesign \
        --force \
        --options runtime \
        --timestamp \
        --entitlements "$script_directory/entitlements.plist" \
        --sign "$application_identity" \
        "$app_path"
    signing_kind="Developer ID"
else
    for library in "$frameworks_path"/*.dylib; do
        codesign --force --options runtime --timestamp=none --sign - "$library"
    done
    codesign \
        --force \
        --options runtime \
        --timestamp=none \
        --entitlements "$script_directory/entitlements-ci.plist" \
        --sign - \
        "$app_path"
    signing_kind="ad hoc"
fi

codesign --verify --deep --strict --verbose=2 "$app_path"

pkgbuild \
    --component "$app_path" \
    --identifier com.oramasearch.liquify \
    --install-location /Applications \
    --version "$version" \
    "$signed_component"

if [[ -n "$installer_identity" ]]; then
    productbuild --sign "$installer_identity" --package "$signed_component" "$final_pkg"
else
    productbuild --package "$signed_component" "$final_pkg"
fi

hdiutil create \
    -fs HFS+ \
    -format UDZO \
    -srcfolder "$app_path" \
    -volname Liquify \
    "$final_dmg"

if [[ -n "$application_identity" ]]; then
    codesign \
        --force \
        --options runtime \
        --timestamp \
        --sign "$application_identity" \
        "$final_dmg"
else
    codesign --force --options runtime --timestamp=none --sign - "$final_dmg"
fi

notary_ready=false
if [[ -n "$application_identity" && \
      -n "$installer_identity" && \
      -n "$notary_key_path" && \
      -n "$notary_key_id" && \
      -n "$notary_issuer_id" ]]; then
    notary_ready=true
fi

if [[ "$notary_ready" == true ]]; then
    for artifact in "$final_pkg" "$final_dmg"; do
        xcrun notarytool submit "$artifact" \
            --key "$notary_key_path" \
            --key-id "$notary_key_id" \
            --issuer "$notary_issuer_id" \
            --wait
        xcrun stapler staple "$artifact"
        xcrun stapler validate "$artifact"
    done
    echo "macOS notarization: run"
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        echo "macOS notarization: run" >> "$GITHUB_STEP_SUMMARY"
    fi
else
    echo "macOS notarization: skipped (App Store Connect credentials unavailable)"
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        echo "macOS notarization: skipped (App Store Connect credentials unavailable)" \
            >> "$GITHUB_STEP_SUMMARY"
    fi
fi

codesign --verify --verbose=2 "$final_dmg"

if [[ -n "$installer_identity" ]]; then
    pkgutil --check-signature "$final_pkg"
fi

mount_output="$(hdiutil attach -nobrowse -readonly "$final_dmg")"
mounted_path="$(printf '%s\n' "$mount_output" | awk -F '\t' '/\/Volumes\// {print $NF; exit}')"
if [[ -z "$mounted_path" ]]; then
    echo "disk image did not report a mounted volume" >&2
    exit 1
fi
mounted_app="$mounted_path/Liquify.app"
codesign --verify --deep --strict --verbose=2 "$mounted_app"
if otool -L "$mounted_app/Contents/MacOS/liquify" | grep -Eq '/opt/homebrew|/usr/local'; then
    echo "packaged executable retains a package-manager library path" >&2
    exit 1
fi
"$mounted_app/Contents/MacOS/liquify" version
hdiutil detach "$mounted_path"
mounted_path=""

if spctl --assess --type install --verbose=4 "$final_pkg"; then
    echo "macOS Gatekeeper package assessment: passed"
elif [[ "$notary_ready" == true ]]; then
    echo "notarized package failed Gatekeeper assessment" >&2
    exit 1
else
    echo "macOS Gatekeeper package assessment: expected rejection without notarization"
fi

if spctl --assess --type open --context context:primary-signature --verbose=4 "$final_dmg"; then
    echo "macOS Gatekeeper disk image assessment: passed"
elif [[ "$notary_ready" == true ]]; then
    echo "notarized disk image failed Gatekeeper assessment" >&2
    exit 1
else
    echo "macOS Gatekeeper disk image assessment: expected rejection without notarization"
fi

(
    cd "$output_directory"
    shasum -a 256 "$(basename "$final_pkg")" "$(basename "$final_dmg")"
) > "$output_directory/liquify-${version}-macos.sha256"

echo "macOS signing: $signing_kind"
echo "macOS artifacts: $final_pkg $final_dmg"
