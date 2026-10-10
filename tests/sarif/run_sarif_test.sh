#!/bin/bash
# Run a DTT test with SARIF output and validate the result.
# Usage: bash run_sarif_test.sh <clang++> <source.cpp> <libDTTPlugin.so> <validator-args...>
#
# Example:
#   bash run_sarif_test.sh /usr/bin/clang++-18 examples/02_basic_fail.cpp build/libDTTPlugin.so \
#       --expect-rule DTT-precondition --expect-min-results 2

set -euo pipefail

CLANGXX="$1"
SOURCE="$2"
PLUGIN="$3"
shift 3

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SARIF_TMP="$(mktemp /tmp/dtt_sarif_XXXXXX.json)"
trap "rm -f '$SARIF_TMP'" EXIT

# Run DTT with SARIF output (ignore compilation errors from DTT)
"$CLANGXX" -fplugin="$PLUGIN" \
    -fplugin-arg-dtt---sarif="$SARIF_TMP" \
    -fplugin-arg-dtt---dtt-mode=enforce \
    -std=c++20 -fsyntax-only "$SOURCE" 2>/dev/null || true

# Validate SARIF output
python3 "$SCRIPT_DIR/validate_sarif.py" "$SARIF_TMP" "$@"
