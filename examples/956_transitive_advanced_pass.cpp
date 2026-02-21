// 956_transitive_advanced_pass.cpp
// Tests: Advanced transitive precondition patterns (P1 Medium)
//
// This test covers:
// 1. Return value propagation - function returns buffer
// 2. Output parameter - size passed via pointer
// 3. Conditional size - ternary in size expression
// 4. Nested member access - s->inner->buf
//
// Expected: BUG cases warn, OK cases don't warn.

#include <cstring>

// =============================================================================
// HELPER: Inner function that calls memcpy (infers precondition)
// =============================================================================

static void copy_n(char* dst, const char* src, size_t n) {
    memcpy(dst, src, n);  // Infers: dst._size_bytes >= n
}

// =============================================================================
// TEST 1: Return value propagation
// Function returns a buffer, caller uses it
// =============================================================================

struct BufferHolder {
    char buf[20];
};

static char* get_buffer(BufferHolder* h) {
    return h->buf;  // Returns pointer to 20-byte buffer
}

static void return_value_wrapper(BufferHolder* h, size_t n) {
    char* p = get_buffer(h);
    copy_n(p, "data", n);  // Requires: p (h->buf) size >= n
}

void test_return_value_bug() {
    BufferHolder h;
    return_value_wrapper(&h, 25);  // GAP: buf is 20 bytes, need 25 (return value tracking)
}

void test_return_value_ok() {
    BufferHolder h;
    return_value_wrapper(&h, 15);  // OK: buf is 20 bytes, need 15
}

void test_return_value_exact() {
    BufferHolder h;
    return_value_wrapper(&h, 20);  // OK: buf is 20 bytes, need 20
}

// =============================================================================
// TEST 2: Output parameter - size via pointer
// =============================================================================

static void get_size(size_t* out_size, size_t val) {
    *out_size = val;
}

static void output_param_wrapper(char* buf, size_t input_size) {
    size_t n = 0;
    get_size(&n, input_size);
    copy_n(buf, "data", n);  // Requires: buf._size >= n (which equals input_size)
}

void test_output_param_bug() {
    char small[10];
    output_param_wrapper(small, 15);  // BUG: 10 < 15
}

void test_output_param_ok() {
    char large[20];
    output_param_wrapper(large, 15);  // OK: 20 >= 15
}

// =============================================================================
// TEST 3: Conditional size - ternary expression
// =============================================================================

static void conditional_wrapper(char* buf, size_t n, size_t m, bool use_n) {
    copy_n(buf, "data", use_n ? n : m);  // Requires: buf._size >= (use_n ? n : m)
}

void test_conditional_bug_n() {
    char small[10];
    conditional_wrapper(small, 15, 5, true);  // BUG: uses n=15, buf is 10
}

void test_conditional_bug_m() {
    char small[10];
    conditional_wrapper(small, 5, 15, false);  // GAP: uses m=15, buf is 10 (path-sensitive)
}

void test_conditional_ok_n() {
    char buf[20];
    conditional_wrapper(buf, 15, 25, true);  // OK: uses n=15, buf is 20
}

void test_conditional_ok_m() {
    char buf[20];
    conditional_wrapper(buf, 25, 15, false);  // GAP-FP: false positive due to ternary
}

// Simpler: conditional with constant
static void conditional_const_wrapper(char* buf, size_t n, bool large) {
    copy_n(buf, "data", large ? n + 10 : n);  // size is n or n+10
}

void test_conditional_const_bug() {
    char small[15];
    conditional_const_wrapper(small, 10, true);  // GAP: 15 < 10+10=20 (ternary not evaluated)
}

void test_conditional_const_ok() {
    char large[25];
    conditional_const_wrapper(large, 10, true);  // OK: 25 >= 20
}

// =============================================================================
// TEST 4: Nested member access - embedded structs (s.inner.buf)
// =============================================================================

struct Inner {
    char buf[15];
    int other;
};

struct OuterEmbedded {
    Inner inner;  // Embedded, not pointer
    int flags;
};

static void nested_embedded_wrapper(OuterEmbedded* o, size_t n) {
    copy_n(o->inner.buf, "data", n);  // Requires: o->inner.buf._size >= n
}

void test_nested_embedded_bug() {
    OuterEmbedded o;
    o.inner.other = 0;
    o.flags = 0;
    nested_embedded_wrapper(&o, 20);  // BUG: inner.buf is 15 bytes
}

void test_nested_embedded_ok() {
    OuterEmbedded o;
    o.inner.other = 0;
    o.flags = 0;
    nested_embedded_wrapper(&o, 10);  // OK: inner.buf is 15 bytes
}

void test_nested_embedded_exact() {
    OuterEmbedded o;
    o.inner.other = 0;
    o.flags = 0;
    nested_embedded_wrapper(&o, 15);  // OK: inner.buf is exactly 15 bytes
}

// Double nesting with embedded structs
struct Level1 {
    char data[10];
};

struct Level2 {
    Level1 l1;  // Embedded
};

struct Level3 {
    Level2 l2;  // Embedded
};

static void deep_embedded_wrapper(Level3* l3, size_t n) {
    copy_n(l3->l2.l1.data, "data", n);  // 3 levels deep, all embedded
}

void test_deep_embedded_bug() {
    Level3 l3;
    deep_embedded_wrapper(&l3, 15);  // BUG: data is 10 bytes
}

void test_deep_embedded_ok() {
    Level3 l3;
    deep_embedded_wrapper(&l3, 8);  // OK: data is 10 bytes
}

// =============================================================================
// TEST 5: Nested embedded member with offset
// =============================================================================

static void nested_embedded_offset_wrapper(OuterEmbedded* o, size_t n, size_t off) {
    copy_n(o->inner.buf + off, "data", n);  // Nested embedded + offset
}

void test_nested_embedded_offset_bug() {
    OuterEmbedded o;
    o.inner.other = 0;
    o.flags = 0;
    nested_embedded_offset_wrapper(&o, 10, 8);  // BUG: 15 < 10+8=18
}

void test_nested_embedded_offset_ok() {
    OuterEmbedded o;
    o.inner.other = 0;
    o.flags = 0;
    nested_embedded_offset_wrapper(&o, 5, 8);  // OK: 15 >= 5+8=13
}

// =============================================================================
// TEST 6: Member array of different types
// =============================================================================

struct TypedArrays {
    int ints[5];      // 20 bytes
    double doubles[3]; // 24 bytes
    short shorts[10]; // 20 bytes
};

static void int_member_wrapper(TypedArrays* t, size_t bytes) {
    memcpy(t->ints, "data", bytes);
}

static void double_member_wrapper(TypedArrays* t, size_t bytes) {
    memcpy(t->doubles, "data", bytes);
}

void test_int_member_bug() {
    TypedArrays t;
    int_member_wrapper(&t, 25);  // BUG: ints is 20 bytes
}

void test_int_member_ok() {
    TypedArrays t;
    int_member_wrapper(&t, 16);  // OK: ints is 20 bytes
}

void test_double_member_bug() {
    TypedArrays t;
    double_member_wrapper(&t, 30);  // BUG: doubles is 24 bytes
}

void test_double_member_ok() {
    TypedArrays t;
    double_member_wrapper(&t, 20);  // OK: doubles is 24 bytes
}

int main() {
    // Return value tests
    test_return_value_bug();
    test_return_value_ok();
    test_return_value_exact();

    // Output parameter tests
    test_output_param_bug();
    test_output_param_ok();

    // Conditional tests
    test_conditional_bug_n();
    test_conditional_bug_m();
    test_conditional_ok_n();
    test_conditional_ok_m();
    test_conditional_const_bug();
    test_conditional_const_ok();

    // Nested embedded member tests
    test_nested_embedded_bug();
    test_nested_embedded_ok();
    test_nested_embedded_exact();
    test_deep_embedded_bug();
    test_deep_embedded_ok();

    // Nested embedded with offset
    test_nested_embedded_offset_bug();
    test_nested_embedded_offset_ok();

    // Typed array members
    test_int_member_bug();
    test_int_member_ok();
    test_double_member_bug();
    test_double_member_ok();

    return 0;
}
