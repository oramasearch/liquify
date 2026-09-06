#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)
binary=${1:-"$repo_root/build/liquify"}
output_dir="$repo_root/docs/ui/screenshots"
states=(welcome repository editor diagnostics build assistant review plugins structural)
themes=(light dark)

mkdir -p "$output_dir"
for state in "${states[@]}"; do
    for theme in "${themes[@]}"; do
        output="$output_dir/$state-$theme.png"
        "$binary" --screenshot "$state" --theme "$theme" --out "$output"
    done
done

count=$(find "$output_dir" -maxdepth 1 -type f -name '*.png' | wc -l | tr -d ' ')
if [[ "$count" != "18" ]]; then
    printf 'screenshots: expected 18 PNG files, found %s\n' "$count" >&2
    exit 1
fi

printf 'screenshots: ok — 9 states × 2 themes at 1440x900\n'
