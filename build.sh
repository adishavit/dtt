#!/bin/bash
set -e

# ============================================================
# DTT Plugin Build & Test Script
# ============================================================

echo "=== DTT Plugin Build Script ==="
echo ""

# --- Configuration ---
CLANG_VERSION=${CLANG_VERSION:-18}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Check for Clang ---
if ! command -v clang++-$CLANG_VERSION &> /dev/null; then
    echo "ERROR: clang++-$CLANG_VERSION not found"
    echo "Install with: apt install clang-$CLANG_VERSION libclang-${CLANG_VERSION}-dev llvm-${CLANG_VERSION}-dev"
    exit 1
fi

echo "Using: $(clang++-$CLANG_VERSION --version | head -1)"
echo ""

# --- Build Method 1: Direct compilation (simpler) ---
echo "=== Building plugin (direct compilation) ==="

LLVM_CXXFLAGS=$(llvm-config-$CLANG_VERSION --cxxflags)
LLVM_INCLUDEDIR=$(llvm-config-$CLANG_VERSION --includedir)

clang++-$CLANG_VERSION \
    -shared -fPIC \
    -o "$SCRIPT_DIR/DTTPlugin.so" \
    "$SCRIPT_DIR/src/DTTPlugin.cpp" \
    $LLVM_CXXFLAGS \
    -I"$LLVM_INCLUDEDIR"

echo "Built: $SCRIPT_DIR/DTTPlugin.so"
echo ""

# --- Test ---
echo "=== Running Tests ==="
echo ""

PLUGIN_FLAG="-fplugin=$SCRIPT_DIR/DTTPlugin.so"
INCLUDE_FLAG="-I$SCRIPT_DIR/include"

run_test() {
    local file=$1
    local expect_pass=$2
    local name=$(basename "$file")
    
    echo -n "Testing $name... "
    
    if clang++-$CLANG_VERSION $PLUGIN_FLAG $INCLUDE_FLAG -fsyntax-only "$file" 2>/dev/null; then
        if [ "$expect_pass" = "pass" ]; then
            echo "✓ PASS (compiled)"
        else
            echo "✗ FAIL (should have errored)"
        fi
    else
        if [ "$expect_pass" = "fail" ]; then
            echo "✓ PASS (rejected)"
        else
            echo "✗ FAIL (should have compiled)"
        fi
    fi
}

run_test "$SCRIPT_DIR/examples/01_basic_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/02_basic_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/03_runtime_guards.cpp" pass
run_test "$SCRIPT_DIR/examples/04_runtime_fail.cpp" fail

echo ""
echo "=== Done ==="
echo ""
echo "Usage:"
echo "  clang++ -fplugin=$SCRIPT_DIR/DTTPlugin.so -I$SCRIPT_DIR/include your_file.cpp"
