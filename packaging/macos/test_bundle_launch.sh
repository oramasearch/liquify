#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <Liquify.app>" >&2
    exit 2
fi

source_app="$1"
source_executable="$source_app/Contents/MacOS/liquify"

if [[ ! -x "$source_executable" ]]; then
    echo "bundle executable is not executable: $source_executable" >&2
    exit 1
fi

test_directory="$(mktemp -d "${TMPDIR:-/tmp}/liquify-bundle-launch.XXXXXX")"
copied_app="$test_directory/Liquify.app"

cleanup() {
    rm -rf "$test_directory"
}

trap cleanup EXIT

ditto "$source_app" "$copied_app"
unset DYLD_FALLBACK_FRAMEWORK_PATH
unset DYLD_FALLBACK_LIBRARY_PATH
unset DYLD_FRAMEWORK_PATH
unset DYLD_LIBRARY_PATH
PATH=/usr/bin:/bin:/usr/sbin:/sbin \
    "$copied_app/Contents/MacOS/liquify" version

echo "relocated bundle launch verified: $copied_app"
