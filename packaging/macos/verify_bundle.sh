#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <Liquify.app>" >&2
    exit 2
fi

app_path="$1"
executable_path="$app_path/Contents/MacOS/liquify"
frameworks_path="$app_path/Contents/Frameworks"
macho_count=0

if [[ ! -x "$executable_path" ]]; then
    echo "bundle executable is not executable: $executable_path" >&2
    exit 1
fi

if ! otool -l "$executable_path" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" {
        getline
        getline
        if ($1 == "path" && $2 == "@executable_path/../Frameworks") {
            found = 1
        }
    }
    END { exit found ? 0 : 1 }
'; then
    echo "bundle executable lacks @executable_path/../Frameworks LC_RPATH" >&2
    exit 1
fi

verify_dependency() {
    local macho="$1"
    local dependency="$2"
    local resolved_path

    case "$dependency" in
        /System/Library/* | /usr/lib/*)
            return
            ;;
        @rpath/*)
            resolved_path="$frameworks_path/${dependency#@rpath/}"
            ;;
        @executable_path/*)
            resolved_path="$app_path/Contents/MacOS/${dependency#@executable_path/}"
            ;;
        @loader_path/*)
            resolved_path="$(dirname "$macho")/${dependency#@loader_path/}"
            ;;
        /*)
            echo "absolute non-system dependency in $macho: $dependency" >&2
            exit 1
            ;;
        *)
            echo "unsupported dependency in $macho: $dependency" >&2
            exit 1
            ;;
    esac

    if [[ ! -f "$resolved_path" ]]; then
        echo "bundle-relative dependency does not exist for $macho: $dependency" >&2
        exit 1
    fi
    if ! otool -L "$resolved_path" >/dev/null 2>&1; then
        echo "bundle-relative dependency is not Mach-O for $macho: $dependency" >&2
        exit 1
    fi
}

verify_rpaths() {
    local macho="$1"
    local rpath

    while IFS= read -r rpath; do
        case "$rpath" in
            @*)
                ;;
            /System/Library/* | /usr/lib/*)
                ;;
            /*)
                echo "absolute non-system LC_RPATH in $macho: $rpath" >&2
                exit 1
                ;;
            *)
                echo "relative LC_RPATH in $macho: $rpath" >&2
                exit 1
                ;;
        esac
    done < <(otool -l "$macho" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" {
            getline
            getline
            if ($1 == "path") {
                print $2
            }
        }
    ')
}

while IFS= read -r -d '' candidate; do
    if ! otool -L "$candidate" >/dev/null 2>&1; then
        continue
    fi
    macho_count=$((macho_count + 1))
    while IFS= read -r dependency; do
        verify_dependency "$candidate" "$dependency"
    done < <(otool -L "$candidate" | tail -n +2 | awk '{print $1}')
    verify_rpaths "$candidate"
done < <(find "$app_path/Contents/MacOS" "$frameworks_path" -type f -print0)

if [[ "$macho_count" -eq 0 ]]; then
    echo "bundle contains no Mach-O files: $app_path" >&2
    exit 1
fi

echo "bundle dependencies verified: $macho_count Mach-O files"
