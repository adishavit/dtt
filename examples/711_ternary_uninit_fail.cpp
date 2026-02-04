// 711_ternary_uninit_fail.cpp
// DTT detects uninitialized scalars in ternary operator conditions
// All patterns below should trigger DTT errors

// =============================================================================
// Ternary condition with uninitialized scalar
// =============================================================================

// Variant 1: Simple ternary with uninit condition
int variant1_ternary_condition() {
    int x;
    int y = x > 0 ? 1 : 2;  // ERROR: x is uninitialized in condition
    return y;
}

// Variant 2: Ternary with uninit bool condition
int variant2_bool_condition() {
    bool b;
    int y = b ? 10 : 20;  // ERROR: b is uninitialized
    return y;
}

// Variant 3: Nested ternary with uninit
int variant3_nested_ternary() {
    int x;
    int y = 1;
    int z = x ? (y > 0 ? 1 : 2) : 3;  // ERROR: x is uninitialized
    return z;
}

// Variant 4: Ternary in expression
int variant4_ternary_in_expr() {
    int x;
    int result = 10 + (x < 5 ? 1 : 0);  // ERROR: x is uninitialized
    return result;
}

// Variant 5: Ternary with uninit in both condition and branch
int variant5_multiple_uninit() {
    int x, y;
    int z = x > 0 ? y : 0;  // ERROR: x uninit in condition, y uninit in branch
    return z;
}

// =============================================================================
// Safe patterns (should NOT trigger when gap is fixed)
// =============================================================================

// Safe 1: Initialized condition
int safe1_init_condition() {
    int x = 5;
    int y = x > 0 ? 1 : 2;  // OK: x is initialized
    return y;
}

// Safe 2: Condition from parameter
int safe2_param_condition(int x) {
    int y = x > 0 ? 1 : 2;  // OK: parameter is initialized by caller
    return y;
}

int main() {
    return 0;
}
