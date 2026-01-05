#!/bin/bash
set -e

echo "=== DTT Plugin v2 Build Script ==="
echo ""

CLANG_VERSION=${CLANG_VERSION:-18}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v clang++-$CLANG_VERSION &> /dev/null; then
    echo "ERROR: clang++-$CLANG_VERSION not found"
    echo "Install: apt install clang-$CLANG_VERSION libclang-${CLANG_VERSION}-dev llvm-${CLANG_VERSION}-dev"
    exit 1
fi

echo "Using: $(clang++-$CLANG_VERSION --version | head -1)"
echo ""

echo "=== Building plugin ==="
clang++-$CLANG_VERSION -shared -fPIC \
    -o "$SCRIPT_DIR/DTTPlugin.so" \
    "$SCRIPT_DIR/src/DTTPlugin.cpp" \
    $(llvm-config-$CLANG_VERSION --cxxflags) \
    -I$(llvm-config-$CLANG_VERSION --includedir)

echo "Built: $SCRIPT_DIR/DTTPlugin.so"
echo ""

echo "=== Running Tests ==="
run_test() {
    local file=$1
    local expect=$2
    local name=$(basename "$file")
    echo -n "  $name: "
    if clang++-$CLANG_VERSION -fplugin="$SCRIPT_DIR/DTTPlugin.so" -fsyntax-only "$file" 2>/dev/null; then
        if [ "$expect" = "pass" ]; then echo "✓ PASS"
        else echo "✗ FAIL (should error)"; fi
    else
        if [ "$expect" = "fail" ]; then echo "✓ PASS (rejected)"
        else echo "✗ FAIL (should compile)"; fi
    fi
}

run_test "$SCRIPT_DIR/examples/01_basic_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/02_basic_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/03_runtime_guards.cpp" pass
run_test "$SCRIPT_DIR/examples/04_runtime_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/05_any_property.cpp" pass
run_test "$SCRIPT_DIR/examples/06_compound_constraints.cpp" pass
run_test "$SCRIPT_DIR/examples/07_arithmetic.cpp" pass

echo ""
echo "=== Usage ==="
echo "  clang++ -fplugin=$SCRIPT_DIR/DTTPlugin.so your_file.cpp"
