// 710_uninitialized_scalar_fail.cpp
// DTT detects uninitialized scalar variable usage (CWE-457)
// All variants below trigger DTT errors as expected

// =============================================================================
// Tier 1.1: Uninitialized Scalars - Basic Use
// =============================================================================

// Variant 1: Simple uninitialized int return
int variant1_return_uninit() {
    int x;           // Uninitialized
    return x;        // ERROR: use of uninitialized variable 'x'
}

// Variant 2: Uninitialized double
double variant2_uninit_double() {
    double d;        // Uninitialized
    return d;        // ERROR: use of uninitialized variable 'd'
}

// Variant 3: Uninitialized bool
bool variant3_uninit_bool() {
    bool b;          // Uninitialized
    return b;        // ERROR: use of uninitialized variable 'b'
}

// Variant 4: Uninitialized char
char variant4_uninit_char() {
    char c;          // Uninitialized
    return c;        // ERROR: use of uninitialized variable 'c'
}

// Variant 5: Uninitialized in conditional (value used)
int variant5_uninit_in_condition() {
    int x;
    if (x > 0) {     // ERROR: use of uninitialized variable 'x'
        return 1;
    }
    return 0;
}

// =============================================================================
// Tier 1.2: Uninitialized in Expressions
// =============================================================================

// Variant 6: Uninitialized in binary expression
int variant6_binary_expr() {
    int x;
    int y = x + 1;   // ERROR: use of uninitialized variable 'x'
    return y;
}

// Variant 7: Uninitialized in multiplication
int variant7_multiply() {
    int x;
    int y = x * 2;   // ERROR: use of uninitialized variable 'x'
    return y;
}

// Variant 8: Uninitialized in both operands
int variant8_both_uninit() {
    int x, y;
    return x + y;    // ERROR: use of uninitialized variables 'x' and 'y'
}

// Variant 9: Uninitialized in unary expression
int variant9_unary() {
    int x;
    return -x;       // ERROR: use of uninitialized variable 'x'
}

// Variant 10: Uninitialized in increment (reads before write)
int variant10_increment() {
    int x;
    return ++x;      // ERROR: use of uninitialized variable 'x'
}

// =============================================================================
// Tier 1.3: Passing Uninitialized to Functions
// =============================================================================

void sink(int x);
void sink_double(double d);
void sink_ref(int& r);

// Variant 11: Pass uninit to function by value
void variant11_pass_by_value() {
    int x;
    sink(x);         // ERROR: passing uninitialized variable 'x'
}

// Variant 12: Pass uninit double to function
void variant12_pass_double() {
    double d;
    sink_double(d);  // ERROR: passing uninitialized variable 'd'
}

// Variant 13: Pass uninit to multiple params
void sink2(int a, int b);
void variant13_multiple_params() {
    int x, y;
    sink2(x, y);     // ERROR: passing uninitialized variables 'x' and 'y'
}

// Variant 14: Pass uninit to variadic (printf-style)
int printf(const char*, ...);
void variant14_printf() {
    int x;
    printf("%d", x); // ERROR: passing uninitialized variable 'x'
}

// Variant 15: Pass by reference is OK (output param pattern)
void variant15_pass_by_ref_ok() {
    int x;
    sink_ref(x);     // OK: passing address, may be output parameter
    sink(x);         // Still questionable without annotation, but ref call may init
}

// =============================================================================
// Tier 1.4: Returning Uninitialized
// =============================================================================

// Variant 16: Return uninit from non-void function
int variant16_return_int() {
    int result;
    return result;   // ERROR: returning uninitialized variable 'result'
}

// Variant 17: Return uninit from function with branches
int variant17_return_branch(bool cond) {
    int result;
    if (cond) {
        result = 42;
    }
    return result;   // ERROR: 'result' may be uninitialized
}

// Variant 18: Return uninit from nested scope
int variant18_return_nested() {
    int outer;
    {
        int inner;
        outer = inner; // ERROR: 'inner' uninitialized
    }
    return outer;
}

// =============================================================================
// Tier 1.5: Compound Assignment (reads then writes)
// =============================================================================

// Variant 19: Compound add
int variant19_compound_add() {
    int x;
    x += 1;          // ERROR: 'x' read before initialized
    return x;
}

// Variant 20: Compound multiply
int variant20_compound_mul() {
    int x;
    x *= 2;          // ERROR: 'x' read before initialized
    return x;
}

// Variant 21: Compound subtract
int variant21_compound_sub() {
    int x;
    x -= 5;          // ERROR: 'x' read before initialized
    return x;
}

// Variant 22: Compound bitwise
int variant22_compound_bitwise() {
    int x;
    x |= 0xFF;       // ERROR: 'x' read before initialized
    return x;
}

// Variant 23: Post-increment (reads current value)
int variant23_post_increment() {
    int x;
    return x++;      // ERROR: 'x' read before initialized
}

// =============================================================================
// Safe Patterns (should NOT trigger errors)
// =============================================================================

// Safe 1: Initialized variable
int safe1_initialized() {
    int x = 0;       // Initialized
    return x;        // OK
}

// Safe 2: Assigned before use
int safe2_assigned() {
    int x;
    x = 42;          // Assigned
    return x;        // OK
}

// Safe 3: Taking address is OK
void safe3_address_of() {
    int x;
    int* p = &x;     // OK: taking address doesn't read value
    (void)p;
}

// Safe 4: sizeof doesn't evaluate
int safe4_sizeof() {
    int x;
    return sizeof(x); // OK: sizeof doesn't read value
}

// Safe 5: decltype doesn't evaluate
int safe5_decltype() {
    int x;
    decltype(x) y = 0; // OK: decltype doesn't read value
    return y;
}

int main() {
    return 0;
}
