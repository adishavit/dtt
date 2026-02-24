#!/bin/bash
# Run a DTT test with SARIF output and validate the result.
# Usage: run_sarif_test.sh <source.cpp> <libDTTPlugin.so> <validator-args...>
#
# Example:
#   run_sarif_test.sh examples/02_basic_fail.cpp build/libDTTPlugin.so \
#       --expect-rule DTT-precondition --expect-min-results 2

set -euo pipefail

SOURCE="$1"
PLUGIN="$2"
shift 2

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SARIF_TMP="$(mktemp /tmp/dtt_sarif_XXXXXX.json)"
trap "rm -f '$SARIF_TMP'" EXIT

# Run DTT with SARIF output (ignore compilation errors from DTT)
clang++-18 -fplugin="$PLUGIN" \
    -fplugin-arg-dtt---sarif="$SARIF_TMP" \
    -fplugin-arg-dtt---dtt-mode=enforce \
    -std=c++20 -fsyntax-only "$SOURCE" 2>/dev/null || true

# Validate SARIF output
python3 "$SCRIPT_DIR/validate_sarif.py" "$SARIF_TMP" "$@"
