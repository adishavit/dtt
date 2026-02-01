// 953_transitive_expr_pass.cpp
// Tests: Transitive precondition propagation with expressions
//
// DTT propagates preconditions through expressions:
// - Pointer arithmetic: buf + offset ✓
// - Constant offsets: buf + 10 ✓
// - Size expressions: n / 2 ✓
// - Member access: s->buf ✓
//
// NOT YET SUPPORTED:
// - Combined member + offset: s->buf + offset (decomposition works, warning doesn't)
//
// Expected: DTT warns on symbolic expressions (may have false positives
// on safe code until Z3 evaluation is added).

#include <cstring>

// =============================================================================
// Test 1: Pointer arithmetic (buf + offset)
// =============================================================================

static void ptr_inner(char* dst, const char* src, size_t n) {
    memcpy(dst, src, n);  // Infers: dst._size_bytes >= n
}

static void ptr_wrapper(char* buf, size_t n, size_t offset) {
    // Passes buf + offset as dst
    // Should propagate: buf._size_bytes >= n + offset
    ptr_inner(buf + offset, "hello", n);
}

void test_ptr_arithmetic() {
    char small[10];
    ptr_wrapper(small, 8, 5);  // BUG: needs 8 + 5 = 13, only have 10
}

void test_ptr_arithmetic_ok() {
    char large[20];
    ptr_wrapper(large, 8, 5);  // OK: needs 13, have 20
}

// =============================================================================
// Test 2: Constant offset
// =============================================================================

static void const_offset_inner(char* dst, size_t n) {
    memcpy(dst, "data", n);  // Infers: dst._size_bytes >= n
}

static void const_offset_wrapper(char* buf, size_t n) {
    // Passes buf + 10 (constant offset)
    // Should propagate: buf._size_bytes >= n + 10
    const_offset_inner(buf + 10, n);
}

void test_const_offset() {
    char small[15];
    const_offset_wrapper(small, 8);  // BUG: needs 8 + 10 = 18, only have 15
}

void test_const_offset_ok() {
    char large[25];
    const_offset_wrapper(large, 8);  // OK: needs 18, have 25
}

// =============================================================================
// Test 3: Size expression (division)
// =============================================================================

static void size_inner(char* dst, size_t n) {
    memcpy(dst, "data", n);  // Infers: dst._size_bytes >= n
}

static void size_div_wrapper(char* buf, size_t total) {
    // Passes total / 2 as size
    // Should propagate: buf._size_bytes >= total / 2
    size_inner(buf, total / 2);
}

void test_size_div() {
    char small[10];
    size_div_wrapper(small, 30);  // BUG: needs 30/2 = 15, only have 10
}

void test_size_div_ok() {
    char large[20];
    size_div_wrapper(large, 30);  // OK: needs 15, have 20
}

// =============================================================================
// Test 4: Size expression (multiplication)
// =============================================================================

static void size_mul_wrapper(int* buf, size_t count) {
    // Passes count * sizeof(int) as size
    // Should propagate: buf._size_bytes >= count * sizeof(int)
    memcpy(buf, "data", count * sizeof(int));
}

void test_size_mul() {
    int small[2];  // 8 bytes
    size_mul_wrapper(small, 5);  // BUG: needs 5*4 = 20, only have 8
}

void test_size_mul_ok() {
    int large[10];  // 40 bytes
    size_mul_wrapper(large, 5);  // OK: needs 20, have 40
}

// =============================================================================
// Test 5: Member access (s->buf)
// =============================================================================

struct MyStruct {
    char buf[10];
    int other;
};

static void member_inner(char* dst, size_t n) {
    memcpy(dst, "data", n);  // Infers: dst._size_bytes >= n
}

static void member_wrapper(MyStruct* s, size_t n) {
    // Passes s->buf as dst
    // Should propagate: s->buf._size_bytes >= n (or s._buf._size_bytes >= n)
    member_inner(s->buf, n);
}

void test_member_access() {
    MyStruct s;
    member_wrapper(&s, 15);  // BUG: s.buf is only 10 bytes
}

void test_member_access_ok() {
    MyStruct s;
    member_wrapper(&s, 8);  // OK: s.buf is 10 bytes
}

// =============================================================================
// Test 6: Combined - pointer arithmetic with member
// =============================================================================

static void combined_wrapper(MyStruct* s, size_t n, size_t offset) {
    // Passes s->buf + offset as dst
    // Should propagate: s->buf._size_bytes >= n + offset
    member_inner(s->buf + offset, n);
}

void test_combined() {
    MyStruct s;
    combined_wrapper(&s, 5, 8);  // BUG: needs 5 + 8 = 13, s.buf is only 10
}

void test_combined_ok() {
    MyStruct s;
    combined_wrapper(&s, 3, 5);  // OK: needs 8, s.buf is 10
}

int main() {
    test_ptr_arithmetic();
    test_ptr_arithmetic_ok();
    test_const_offset();
    test_const_offset_ok();
    test_size_div();
    test_size_div_ok();
    test_size_mul();
    test_size_mul_ok();
    test_member_access();
    test_member_access_ok();
    test_combined();
    test_combined_ok();
    return 0;
}
