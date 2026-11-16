#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build the tree-sitter AddressSanitizer harness in its own build directory and run it over every
# upstream corpus, highlight test and example of the pinned C and C++ grammars (liquify#92).
# Usage: tools/tree-sitter-asan/run.sh [build-dir] [harness options...]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-${root}/build-tree-sitter-asan}"
shift || true

for module in tree-sitter tree-sitter-c tree-sitter-cpp; do
  if [[ ! -f "${root}/modules/${module}/README.md" ]]; then
    echo "modules/${module} is not checked out: git submodule update --init modules/${module}" >&2
    exit 2
  fi
done

cmake -S "${root}/tools/tree-sitter-asan" -B "${build}" -G Ninja
cmake --build "${build}"

mkdir -p "${build}/reports"
log="${build}/reports/asan.log"
echo "tree-sitter $(git -C "${root}/modules/tree-sitter" rev-parse --short HEAD)," \
  "tree-sitter-c $(git -C "${root}/modules/tree-sitter-c" rev-parse --short HEAD)," \
  "tree-sitter-cpp $(git -C "${root}/modules/tree-sitter-cpp" rev-parse --short HEAD)" | tee "${log}"

# halt_on_error=0 with -fsanitize-recover=address collects every report instead of the first.
export ASAN_OPTIONS="halt_on_error=0:detect_stack_use_after_return=1:strict_string_checks=1:check_initialization_order=1:abort_on_error=0${ASAN_OPTIONS:+:${ASAN_OPTIONS}}"

# First prove the binary is instrumented: a deliberate overflow must produce exactly one report.
if ! "${build}/tree-sitter-asan" --self-test > "${build}/reports/self-test.log" 2>&1; then
  cat "${build}/reports/self-test.log" >&2
  echo "self-test failed: the harness is not catching AddressSanitizer reports" >&2
  exit 3
fi
grep "self-test reports" "${build}/reports/self-test.log" | tee -a "${log}"

status=0
"${build}/tree-sitter-asan" --repro-dir "${build}/reports" "$@" \
  "${root}/modules/tree-sitter-c/test/corpus" \
  "${root}/modules/tree-sitter-c/test/highlight" \
  "${root}/modules/tree-sitter-c/examples" \
  "${root}/modules/tree-sitter-cpp/test/corpus" \
  "${root}/modules/tree-sitter-cpp/test/highlight" \
  "${root}/modules/tree-sitter-cpp/examples" 2>&1 | tee -a "${log}" || status=$?

echo "log: ${log}"
exit "${status}"
