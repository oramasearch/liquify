#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)
binary=${LIQUIFY_BINARY:-"$repo_root/build/liquify"}
output_dir="$repo_root/docs/ui/screenshots"

states=(
    welcome-light
    welcome-dark
    repository-unreadable-light
    repository-unreadable-dark
    lake-offline-light
    lake-offline-dark
    repository-light
    repository-dark
    editor-cpp-light
    editor-cpp-dark
    editor-view-light
    editor-view-dark
    diagnostics-light
    diagnostics-dark
    build-light
    build-dark
    review-light
    review-dark
    assistant-light
    assistant-dark
    plugins-light
    plugins-dark
    structural-editing-light
    structural-editing-dark
    panels-light
    panels-dark
)

if [[ ! -x $binary ]]; then
    printf 'liquify binary is not executable: %s\n' "$binary" >&2
    printf 'build it first or set LIQUIFY_BINARY\n' >&2
    exit 1
fi

mkdir -p "$output_dir"
for state in "${states[@]}"; do
    "$binary" --screenshot "$state" --out "$output_dir/$state.png"
done

printf 'screenshots: ok — 13 screens × 2 themes at 1280x800\n'
