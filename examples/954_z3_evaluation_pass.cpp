// 954_z3_evaluation_pass.cpp
// Tests: Z3 SMT solver evaluation for transitive precondition bounds
//
// Z3 evaluates symbolic expressions like (arg1+arg2) with actual argument
// values to determine if buffer is definitely too small (SAT) or safe (UNSAT).
//
// This test verifies:
// 1. No false positives on safe code (Z3 proves UNSAT)
// 2. True positives on unsafe code (Z3 proves SAT)
// 3. Various expression types: +, -, *, /
// 4. Edge cases: exact bounds, off-by-one, large values
//
// Expected: Only BUG cases should warn, OK cases should NOT warn.

#include <cstring>

// =============================================================================
// HELPER: Inner function that calls memcpy (infers precondition)
// =============================================================================

static void copy_n(char* dst, const char* src, size_t n) {
    memcpy(dst, src, n);  // Infers: dst._size_bytes >= n
}

// =============================================================================
// TEST 1: Addition - (arg1 + arg2)
// =============================================================================

static void add_wrapper(char* buf, size_t n, size_t offset) {
    copy_n(buf + offset, "data", n);  // Requires: buf._size >= n + offset
}

void test_add_bug_1() {
    char small[10];
    add_wrapper(small, 8, 5);  // BUG: 10 < 8+5=13
}

void test_add_bug_2() {
    char small[12];
    add_wrapper(small, 8, 5);  // BUG: 12 < 13
}

void test_add_exact() {
    char exact[13];
    add_wrapper(exact, 8, 5);  // OK: 13 >= 13 (exact fit)
}

void test_add_ok_1() {
    char large[20];
    add_wrapper(large, 8, 5);  // OK: 20 >= 13
}

void test_add_ok_2() {
    char large[100];
    add_wrapper(large, 8, 5);  // OK: 100 >= 13
}

void test_add_zero_offset() {
    char buf[10];
    add_wrapper(buf, 10, 0);  // OK: 10 >= 10+0=10
}

void test_add_zero_size() {
    char buf[5];
    add_wrapper(buf, 0, 10);  // BUG: 5 < 0+10=10 (offset still counts!)
}

// =============================================================================
// TEST 2: Constant offset - (arg1 + CONST)
// =============================================================================

static void const_add_wrapper(char* buf, size_t n) {
    copy_n(buf + 10, "data", n);  // Requires: buf._size >= n + 10
}

void test_const_add_bug() {
    char small[15];
    const_add_wrapper(small, 8);  // BUG: 15 < 8+10=18
}

void test_const_add_exact() {
    char exact[18];
    const_add_wrapper(exact, 8);  // OK: 18 >= 18
}

void test_const_add_ok() {
    char large[25];
    const_add_wrapper(large, 8);  // OK: 25 >= 18
}

// =============================================================================
// TEST 3: Division - (arg1 / CONST)
// =============================================================================

static void div_wrapper(char* buf, size_t total) {
    copy_n(buf, "data", total / 2);  // Requires: buf._size >= total/2
}

void test_div_bug() {
    char small[10];
    div_wrapper(small, 30);  // BUG: 10 < 30/2=15
}

void test_div_exact() {
    char exact[15];
    div_wrapper(exact, 30);  // OK: 15 >= 15
}

void test_div_ok() {
    char large[20];
    div_wrapper(large, 30);  // OK: 20 >= 15
}

void test_div_rounding() {
    char buf[7];
    div_wrapper(buf, 15);  // OK: 7 >= 15/2=7 (integer division rounds down)
}

void test_div_rounding_bug() {
    char buf[6];
    div_wrapper(buf, 15);  // BUG: 6 < 7
}

// =============================================================================
// TEST 4: Multiplication - (arg1 * CONST)
// =============================================================================

static void mul_wrapper(int* buf, size_t count) {
    memcpy(buf, "data", count * sizeof(int));  // Requires: buf._size >= count*4
}

void test_mul_bug() {
    int small[2];  // 8 bytes
    mul_wrapper(small, 5);  // BUG: 8 < 5*4=20
}

void test_mul_exact() {
    int exact[5];  // 20 bytes
    mul_wrapper(exact, 5);  // OK: 20 >= 20
}

void test_mul_ok() {
    int large[10];  // 40 bytes
    mul_wrapper(large, 5);  // OK: 40 >= 20
}

// =============================================================================
// TEST 5: Subtraction - (arg1 - CONST)
// =============================================================================

static void sub_wrapper(char* buf, size_t n) {
    // Only copy n-5 bytes (e.g., skip header)
    if (n > 5) {
        copy_n(buf, "data", n - 5);  // Requires: buf._size >= n-5
    }
}

void test_sub_bug() {
    char small[10];
    sub_wrapper(small, 20);  // BUG: 10 < 20-5=15
}

void test_sub_ok() {
    char large[20];
    sub_wrapper(large, 20);  // OK: 20 >= 15
}

void test_sub_exact() {
    char exact[15];
    sub_wrapper(exact, 20);  // OK: 15 >= 15
}

// =============================================================================
// TEST 6: Complex expressions - (arg1 + arg2 + CONST)
// =============================================================================

static void complex_wrapper(char* buf, size_t n, size_t m) {
    copy_n(buf + 5, "data", n + m);  // Requires: buf._size >= n + m + 5
}

void test_complex_bug() {
    char small[15];
    complex_wrapper(small, 5, 8);  // BUG: 15 < 5+8+5=18
}

void test_complex_ok() {
    char large[20];
    complex_wrapper(large, 5, 8);  // OK: 20 >= 18
}

void test_complex_exact() {
    char exact[18];
    complex_wrapper(exact, 5, 8);  // OK: 18 >= 18
}

// =============================================================================
// TEST 7: Edge cases - large values
// =============================================================================

static void large_wrapper(char* buf, size_t n) {
    copy_n(buf, "data", n);
}

void test_large_values_bug() {
    char small[1000];
    large_wrapper(small, 2000);  // BUG: 1000 < 2000
}

void test_large_values_ok() {
    char large[5000];
    large_wrapper(large, 2000);  // OK: 5000 >= 2000
}

// =============================================================================
// TEST 8: Multiple arguments with same value
// =============================================================================

static void same_args_wrapper(char* buf, size_t n, size_t m) {
    copy_n(buf, "data", n + m);
}

void test_same_args_bug() {
    char small[15];
    same_args_wrapper(small, 10, 10);  // BUG: 15 < 10+10=20
}

void test_same_args_ok() {
    char large[25];
    same_args_wrapper(large, 10, 10);  // OK: 25 >= 20
}

// =============================================================================
// TEST 9: Off-by-one edge cases
// =============================================================================

void test_off_by_one_under() {
    char buf[12];
    add_wrapper(buf, 8, 5);  // BUG: 12 < 13 (off by one)
}

void test_off_by_one_exact() {
    char buf[13];
    add_wrapper(buf, 8, 5);  // OK: 13 >= 13 (exact)
}

void test_off_by_one_over() {
    char buf[14];
    add_wrapper(buf, 8, 5);  // OK: 14 >= 13 (one over)
}

// =============================================================================
// TEST 10: Zero-size operations
// =============================================================================

static void zero_copy_wrapper(char* buf, size_t n) {
    copy_n(buf, "data", n);
}

void test_zero_size_ok() {
    char buf[1];
    zero_copy_wrapper(buf, 0);  // OK: 1 >= 0
}

void test_zero_size_empty_buf() {
    char buf[0];  // Zero-size array (GCC extension)
    // zero_copy_wrapper(buf, 0);  // Edge case - skip for portability
}

int main() {
    // Addition tests
    test_add_bug_1();
    test_add_bug_2();
    test_add_exact();
    test_add_ok_1();
    test_add_ok_2();
    test_add_zero_offset();
    test_add_zero_size();

    // Constant offset tests
    test_const_add_bug();
    test_const_add_exact();
    test_const_add_ok();

    // Division tests
    test_div_bug();
    test_div_exact();
    test_div_ok();
    test_div_rounding();
    test_div_rounding_bug();

    // Multiplication tests
    test_mul_bug();
    test_mul_exact();
    test_mul_ok();

    // Subtraction tests
    test_sub_bug();
    test_sub_ok();
    test_sub_exact();

    // Complex expression tests
    test_complex_bug();
    test_complex_ok();
    test_complex_exact();

    // Large value tests
    test_large_values_bug();
    test_large_values_ok();

    // Same args tests
    test_same_args_bug();
    test_same_args_ok();

    // Off-by-one tests
    test_off_by_one_under();
    test_off_by_one_exact();
    test_off_by_one_over();

    // Zero-size tests
    test_zero_size_ok();

    return 0;
}
