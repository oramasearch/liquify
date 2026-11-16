#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <liquify-binary> <linuxdeploy> <output-directory>" >&2
    exit 2
fi

binary_path="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
linuxdeploy_path="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
output_directory="$3"
script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
version="${LIQUIFY_PACKAGE_VERSION:-0.0.1}"

if [[ ! -x "$binary_path" ]]; then
    echo "liquify binary is not executable: $binary_path" >&2
    exit 1
fi

if [[ ! -x "$linuxdeploy_path" ]]; then
    echo "linuxdeploy is not executable: $linuxdeploy_path" >&2
    exit 1
fi

mkdir -p "$output_directory"
output_directory="$(cd "$output_directory" && pwd)"
work_directory="$(mktemp -d "${TMPDIR:-/tmp}/liquify-appimage.XXXXXX")"
trap 'rm -rf "$work_directory"' EXIT

app_directory="$work_directory/Liquify.AppDir"
artifact="$output_directory/liquify-${version}-x86_64.AppImage"
mkdir -p "$app_directory/usr/bin"
install -m 755 "$binary_path" "$app_directory/usr/bin/liquify"
cp -R "$(dirname "$binary_path")/frontends" "$app_directory/usr/bin/frontends"
install -m 755 "$script_directory/AppRun" "$app_directory/AppRun"
install -m 644 \
    "$script_directory/com.oramasearch.Liquify.desktop" \
    "$app_directory/com.oramasearch.Liquify.desktop"
install -m 644 \
    "$script_directory/com.oramasearch.Liquify.svg" \
    "$app_directory/com.oramasearch.Liquify.svg"

OUTPUT="$artifact" "$linuxdeploy_path" \
    --appimage-extract-and-run \
    --appdir "$app_directory" \
    --executable "$app_directory/usr/bin/liquify" \
    --desktop-file "$app_directory/com.oramasearch.Liquify.desktop" \
    --icon-file "$app_directory/com.oramasearch.Liquify.svg" \
    --output appimage
chmod 755 "$artifact"
APPIMAGE_EXTRACT_AND_RUN=1 "$artifact" version
(
    cd "$output_directory"
    sha256sum "$(basename "$artifact")"
) > "$artifact.sha256"

echo "AppImage artifact: $artifact"
