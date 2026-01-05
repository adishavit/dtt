#!/bin/bash
set -e

echo "=== DTT Plugin v15 Build Script ==="
echo ""

CLANG_VERSION=${CLANG_VERSION:-18}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v clang++-$CLANG_VERSION &> /dev/null; then
    echo "ERROR: clang++-$CLANG_VERSION not found"
    exit 1
fi

echo "Using: $(clang++-$CLANG_VERSION --version | head -1)"
echo "Z3: $(z3 --version 2>/dev/null || echo 'not found')"
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
PLUGIN="-fplugin=$SCRIPT_DIR/DTTPlugin.so"

run_test() {
    local file=$1
    local expect=$2
    local name=$(basename "$file")
    echo -n "  $name: "
    if clang++-$CLANG_VERSION $PLUGIN -fsyntax-only "$file" 2>/dev/null; then
        if [ "$expect" = "pass" ]; then echo "✓ PASS"
        else echo "✗ FAIL (should error)"; fi
    else
        if [ "$expect" = "fail" ]; then echo "✓ PASS (rejected)"
        else echo "✗ FAIL (should compile)"; fi
    fi
}

echo "--- Basic Tests ---"
run_test "$SCRIPT_DIR/examples/01_basic_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/02_basic_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/03_runtime_guards.cpp" pass
run_test "$SCRIPT_DIR/examples/04_runtime_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/05_any_property.cpp" pass
run_test "$SCRIPT_DIR/examples/06_compound_constraints.cpp" pass
run_test "$SCRIPT_DIR/examples/07_arithmetic.cpp" pass
echo "--- Postconditions ---"
run_test "$SCRIPT_DIR/examples/08_postcond_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/09_postcond_fail.cpp" fail
echo "--- Guards ---"
run_test "$SCRIPT_DIR/examples/10_else_guard_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/11_else_guard_fail.cpp" fail
echo "--- Proof Persistence ---"
run_test "$SCRIPT_DIR/examples/12_proof_persist.cpp" pass
run_test "$SCRIPT_DIR/examples/13_proof_copy.cpp" pass
run_test "$SCRIPT_DIR/examples/14_proof_invalidate.cpp" fail
echo "--- Struct Fields ---"
run_test "$SCRIPT_DIR/examples/15_struct_field_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/16_struct_field_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/17_nested_struct_pass.cpp" pass
echo "--- Z3 SMT ---"
run_test "$SCRIPT_DIR/examples/18_z3_arithmetic_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/19_z3_arithmetic_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/20_z3_symbolic_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/21_z3_chain_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/22_z3_chain_fail.cpp" fail
echo "--- Multi-Arg Constraints ---"
run_test "$SCRIPT_DIR/examples/23_multiarg_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/24_multiarg_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/25_multiarg_symbolic_pass.cpp" pass
echo "--- Switch Totality ---"
run_test "$SCRIPT_DIR/examples/26_switch_total_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/27_switch_total_fail.cpp" fail
echo "--- Array Indices ---"
run_test "$SCRIPT_DIR/examples/28_array_const_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/29_array_const_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/30_array_guard_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/31_array_runtime_invalidate.cpp" fail
run_test "$SCRIPT_DIR/examples/32_array_different_indices_pass.cpp" pass
echo "--- old() Postconditions ---"
run_test "$SCRIPT_DIR/examples/33_old_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/34_old_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/35_old_chain_pass.cpp" pass
echo "--- Boolean Return Postconditions ---"
run_test "$SCRIPT_DIR/examples/36_bool_return_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/37_bool_return_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/38_bool_true_branch_pass.cpp" pass
echo "--- Method Call Tracking ---"
run_test "$SCRIPT_DIR/examples/39_method_returns_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/40_method_returns_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/41_method_requires_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/42_method_requires_fail.cpp" fail
run_test "$SCRIPT_DIR/examples/43_method_guard_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/44_method_bool_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/45_dtt_vector_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/46_dtt_vector_fail.cpp" fail

echo ""
echo "=== Usage ==="
echo "  clang++ -fplugin=$SCRIPT_DIR/DTTPlugin.so your_file.cpp"

echo "--- Index Bounds ---"
run_test "$SCRIPT_DIR/examples/47_index_bounds_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/48_index_bounds_fail.cpp" fail

echo "--- Symbolic Arguments ---"
run_test "$SCRIPT_DIR/examples/50_symbolic_arg_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/51_symbolic_arg_fail.cpp" fail

echo "--- Loop Bounds & Runtime Guards ---"
run_test "$SCRIPT_DIR/examples/52_loop_bounds_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/53_guarded_index_pass.cpp" pass

echo "--- Arithmetic Expressions ---"
run_test "$SCRIPT_DIR/examples/54_arithmetic_expr_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/55_arithmetic_expr_fail.cpp" fail

echo "--- Transitive Proofs ---"
run_test "$SCRIPT_DIR/examples/56_transitive_proof_pass.cpp" pass

echo "--- Index Arithmetic (v15) ---"
run_test "$SCRIPT_DIR/examples/57_index_arithmetic_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/58_index_arithmetic_fail.cpp" fail

echo "--- Return Size Relationships (v15) ---"
run_test "$SCRIPT_DIR/examples/59_return_size_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/60_return_size_fail.cpp" fail

echo "--- Symbolic Division (v15) ---"
run_test "$SCRIPT_DIR/examples/61_symbolic_div_pass.cpp" pass

echo "--- Vector Count Constructor (v15.1) ---"
run_test "$SCRIPT_DIR/examples/62_vector_count_ctor_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/63_vector_count_ctor_fail.cpp" fail

echo "--- Vector Assign (v15.1) ---"
run_test "$SCRIPT_DIR/examples/64_vector_assign_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/65_vector_assign_fail.cpp" fail

echo "--- Copy/Move Assignment (v16) ---"
run_test "$SCRIPT_DIR/examples/66_assignment_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/67_assignment_fail.cpp" fail

echo "--- Swap (v16) ---"
run_test "$SCRIPT_DIR/examples/68_swap_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/69_swap_fail.cpp" fail

echo "--- Move Semantics (v16) ---"
run_test "$SCRIPT_DIR/examples/70_move_invalidate_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/71_move_invalidate_fail.cpp" fail

echo "--- Self-Assignment (v16) ---"
run_test "$SCRIPT_DIR/examples/72_self_assign_pass.cpp" pass

echo "--- Complex Assignment Patterns (v16) ---"
run_test "$SCRIPT_DIR/examples/73_complex_patterns_pass.cpp" pass
run_test "$SCRIPT_DIR/examples/74_complex_patterns_fail.cpp" fail
