// 955_member_path_pass.cpp
// Tests: Member path resolution for transitive precondition warnings
//
// When a precondition involves a struct member (e.g., s->buf), DTT must
// resolve the member path to get the actual buffer size for verification.
//
// This test verifies:
// 1. Simple member access: s->buf
// 2. Different member array sizes
// 3. Pointer vs reference struct access
// 4. Multiple struct members
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
// TEST 1: Simple struct member access
// =============================================================================

struct SimpleStruct {
    char buf[10];
    int other;
};

static void simple_member_wrapper(SimpleStruct* s, size_t n) {
    copy_n(s->buf, "data", n);  // Requires: s->buf._size >= n
}

void test_simple_member_bug() {
    SimpleStruct s;
    simple_member_wrapper(&s, 15);  // BUG: s.buf is 10 bytes, need 15
}

void test_simple_member_ok() {
    SimpleStruct s;
    simple_member_wrapper(&s, 8);  // OK: s.buf is 10 bytes, need 8
}

void test_simple_member_exact() {
    SimpleStruct s;
    simple_member_wrapper(&s, 10);  // OK: s.buf is 10 bytes, need 10
}

// =============================================================================
// TEST 2: Different member sizes
// =============================================================================

struct SmallBufStruct {
    char buf[5];
};

struct LargeBufStruct {
    char buf[100];
};

static void small_buf_wrapper(SmallBufStruct* s, size_t n) {
    copy_n(s->buf, "data", n);
}

static void large_buf_wrapper(LargeBufStruct* s, size_t n) {
    copy_n(s->buf, "data", n);
}

void test_small_buf_bug() {
    SmallBufStruct s;
    small_buf_wrapper(&s, 10);  // BUG: buf is 5 bytes
}

void test_small_buf_ok() {
    SmallBufStruct s;
    small_buf_wrapper(&s, 4);  // OK: buf is 5 bytes
}

void test_large_buf_ok() {
    LargeBufStruct s;
    large_buf_wrapper(&s, 50);  // OK: buf is 100 bytes
}

void test_large_buf_bug() {
    LargeBufStruct s;
    large_buf_wrapper(&s, 150);  // BUG: buf is 100 bytes
}

// =============================================================================
// TEST 3: Reference vs pointer access
// =============================================================================

static void ref_member_wrapper(SimpleStruct& s, size_t n) {
    copy_n(s.buf, "data", n);  // Requires: s.buf._size >= n
}

void test_ref_member_bug() {
    SimpleStruct s;
    ref_member_wrapper(s, 15);  // BUG: s.buf is 10 bytes
}

void test_ref_member_ok() {
    SimpleStruct s;
    ref_member_wrapper(s, 8);  // OK: s.buf is 10 bytes
}

// =============================================================================
// TEST 4: Multiple struct members
// =============================================================================

struct MultiMemberStruct {
    char small_buf[5];
    char large_buf[50];
    int count;
};

static void multi_small_wrapper(MultiMemberStruct* s, size_t n) {
    copy_n(s->small_buf, "data", n);
}

static void multi_large_wrapper(MultiMemberStruct* s, size_t n) {
    copy_n(s->large_buf, "data", n);
}

void test_multi_small_bug() {
    MultiMemberStruct s;
    multi_small_wrapper(&s, 10);  // BUG: small_buf is 5 bytes
}

void test_multi_small_ok() {
    MultiMemberStruct s;
    multi_small_wrapper(&s, 4);  // OK: small_buf is 5 bytes
}

void test_multi_large_bug() {
    MultiMemberStruct s;
    multi_large_wrapper(&s, 60);  // BUG: large_buf is 50 bytes
}

void test_multi_large_ok() {
    MultiMemberStruct s;
    multi_large_wrapper(&s, 40);  // OK: large_buf is 50 bytes
}

// =============================================================================
// TEST 5: Integer array members
// =============================================================================

struct IntArrayStruct {
    int data[10];  // 40 bytes
};

static void int_array_wrapper(IntArrayStruct* s, size_t bytes) {
    memcpy(s->data, "source", bytes);  // Requires: s->data._size >= bytes
}

void test_int_array_bug() {
    IntArrayStruct s;
    int_array_wrapper(&s, 50);  // BUG: data is 40 bytes
}

void test_int_array_ok() {
    IntArrayStruct s;
    int_array_wrapper(&s, 30);  // OK: data is 40 bytes
}

void test_int_array_exact() {
    IntArrayStruct s;
    int_array_wrapper(&s, 40);  // OK: data is exactly 40 bytes
}

// =============================================================================
// TEST 6: Off-by-one with members
// =============================================================================

void test_member_off_by_one_under() {
    SimpleStruct s;
    simple_member_wrapper(&s, 11);  // BUG: 10 < 11
}

void test_member_off_by_one_exact() {
    SimpleStruct s;
    simple_member_wrapper(&s, 10);  // OK: 10 >= 10
}

void test_member_off_by_one_over() {
    SimpleStruct s;
    simple_member_wrapper(&s, 9);  // OK: 10 >= 9
}

// =============================================================================
// TEST 7: Zero-size operations on members
// =============================================================================

void test_member_zero_size() {
    SimpleStruct s;
    simple_member_wrapper(&s, 0);  // OK: 10 >= 0
}

// =============================================================================
// TEST 8: Expression with member access
// =============================================================================

static void member_with_offset_wrapper(SimpleStruct* s, size_t n, size_t off) {
    copy_n(s->buf + off, "data", n);  // Requires: s->buf._size >= n + off
}

void test_member_offset_bug() {
    SimpleStruct s;
    member_with_offset_wrapper(&s, 5, 8);  // BUG: 10 < 5+8=13
}

void test_member_offset_ok() {
    SimpleStruct s;
    member_with_offset_wrapper(&s, 3, 5);  // OK: 10 >= 3+5=8
}

void test_member_offset_exact() {
    SimpleStruct s;
    member_with_offset_wrapper(&s, 5, 5);  // OK: 10 >= 5+5=10
}

int main() {
    // Simple member tests
    test_simple_member_bug();
    test_simple_member_ok();
    test_simple_member_exact();

    // Different sizes
    test_small_buf_bug();
    test_small_buf_ok();
    test_large_buf_ok();
    test_large_buf_bug();

    // Reference access
    test_ref_member_bug();
    test_ref_member_ok();

    // Multiple members
    test_multi_small_bug();
    test_multi_small_ok();
    test_multi_large_bug();
    test_multi_large_ok();

    // Integer arrays
    test_int_array_bug();
    test_int_array_ok();
    test_int_array_exact();

    // Off-by-one
    test_member_off_by_one_under();
    test_member_off_by_one_exact();
    test_member_off_by_one_over();

    // Zero size
    test_member_zero_size();

    // Member with offset
    test_member_offset_bug();
    test_member_offset_ok();
    test_member_offset_exact();

    return 0;
}
