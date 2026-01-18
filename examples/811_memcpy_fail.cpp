// 811_memcpy_fail.cpp
// Tests: Unsafe memcpy/memmove/memset/memcmp/memchr usage that DTT catches (CVE patterns)
// DTT detects buffer overflows and over-reads for C memory functions with constant sizes.
// Reference: PHASE1_C_MEMORY_FUNCTIONS.md

#include <cstring>
#include <cstddef>

// =============================================================================
// SECTION 1: Obvious overflow (destination too small)
// =============================================================================

// Variant 1.1: CVE-2024-24450 pattern - obvious overflow
void test_obvious_overflow() {
    char dest[10];
    char src[100];
    memcpy(dest, src, 100);  // ERROR: 100 > dest._capacity (10)
}

// Variant 1.2: sizeof wrong buffer
void test_wrong_sizeof() {
    char dest[10];
    char src[100];
    memcpy(dest, src, sizeof(src));  // ERROR: 100 > 10
}

// Variant 1.3: hard-coded size exceeds buffer
void test_hardcoded_overflow() {
    char buf[32];
    memcpy(buf, "x", 64);  // ERROR: 64 > 32
}

// =============================================================================
// SECTION 2: Source too small
// =============================================================================

// Variant 2.1: reading beyond source buffer
void test_src_overflow() {
    char dest[100];
    char src[10];
    memcpy(dest, src, 50);  // ERROR: 50 > src._capacity (10)
}

// Variant 2.2: src sizeof mismatch
void test_src_sizeof_mismatch() {
    char dest[100];
    char src[20];
    memcpy(dest, src, sizeof(dest));  // ERROR: 100 > src._capacity (20)
}

// =============================================================================
// SECTION 3: Pointer arithmetic overflow
// =============================================================================

// Variant 3.1: CVE-2023-38545 pattern - offset reduces capacity
void test_ptr_overflow() {
    char buf[100];
    char* p = buf + 90;
    memcpy(p, "hello world", 12);  // ERROR: 12 > 10 (100 - 90)
}

// Variant 3.2: double offset overflow
void test_double_offset() {
    char buf[100];
    char* p = buf + 50;
    char* q = p + 40;  // q._capacity = 10
    memcpy(q, "0123456789X", 11);  // ERROR: 11 > 10
}

// Variant 3.3: offset from sizeof, still overflows
void test_offset_sizeof() {
    char buf[64];
    memcpy(buf + 60, "hello", 6);  // ERROR: 6 > 4 (64 - 60)
}

// =============================================================================
// SECTION 4: Unguarded symbolic size (Heartbleed pattern)
// =============================================================================

// Variant 4.1: symbolic size from parameter
void test_unguarded_param(size_t payload_len) {
    char buf[256];
    memcpy(buf, "data", payload_len);  // ERROR: can't prove payload_len <= 256
}

// Variant 4.2: CVE-like network buffer
void test_network_buffer(const char* data, size_t data_len) {
    char local_buf[128];
    memcpy(local_buf, data, data_len);  // ERROR: data_len unguarded
}

// Variant 4.3: size from untrusted source
void test_untrusted_size(size_t user_size) {
    char buf[64];
    memset(buf, 0, user_size);  // ERROR: user_size unguarded
}

// =============================================================================
// SECTION 5: memmove overflow
// =============================================================================

// Variant 5.1: CVE-2022-26937 pattern - memmove overflow
void test_memmove_overflow() {
    char buffer[96];
    char src[200];
    memmove(buffer, src, 150);  // ERROR: 150 > 96
}

// Variant 5.2: memmove with strlen (unknown length)
void test_memmove_strlen(const char* s) {
    char buf[100];
    memmove(buf, s, strlen(s));  // ERROR: strlen(s) unguarded
}

// =============================================================================
// SECTION 6: memset overflow
// =============================================================================

// Variant 6.1: memset beyond buffer
void test_memset_overflow() {
    char buf[50];
    memset(buf, 0, 100);  // ERROR: 100 > 50
}

// Variant 6.2: memset with ptr arithmetic overflow
void test_memset_ptr_overflow() {
    char buf[64];
    memset(buf + 60, 'x', 10);  // ERROR: 10 > 4
}

// Variant 6.3: memset unguarded symbolic
void test_memset_unguarded(size_t n) {
    char buf[32];
    memset(buf, 0, n);  // ERROR: n unguarded
}

// =============================================================================
// SECTION 7: memcmp overflow
// =============================================================================

// Variant 7.1: memcmp reading beyond buffer
void test_memcmp_overflow() {
    char a[10], b[100];
    memcmp(a, b, 50);  // ERROR: 50 > a._capacity (10)
}

// Variant 7.2: memcmp both buffers too small
void test_memcmp_both_small() {
    char a[5], b[5];
    memcmp(a, b, 10);  // ERROR: 10 > 5
}

// =============================================================================
// SECTION 8: memchr overflow
// =============================================================================

// Variant 8.1: memchr beyond buffer
void test_memchr_overflow() {
    char buf[10];
    memchr(buf, 'x', 50);  // ERROR: 50 > 10
}

// =============================================================================
// SECTION 9: Off-by-one errors
// =============================================================================

// Variant 9.1: exactly one byte over
void test_off_by_one() {
    char buf[10];
    memcpy(buf, "0123456789", 11);  // ERROR: 11 > 10 (off-by-one)
}

// Variant 9.2: sizeof includes null but copy doesn't
void test_off_by_one_null() {
    char dest[5];
    const char* src = "hello";  // 5 chars + null = 6 bytes
    memcpy(dest, src, 6);       // ERROR: 6 > 5
}

// =============================================================================
// SECTION 10: Struct member overflow
// =============================================================================

// Variant 10.1: memcpy into struct member
void test_struct_member_overflow() {
    struct S { char name[10]; int id; };
    S s;
    memcpy(s.name, "this is way too long", 20);  // ERROR: 20 > 10
}

// Variant 10.2: memset struct member overflow
void test_struct_memset_overflow() {
    struct S { int x; char buf[8]; };
    S s;
    memset(&s.buf, 0, 20);  // ERROR: 20 > 8
}

// =============================================================================
// SECTION 11: Wrong guard condition
// =============================================================================

// Variant 11.1: guard checks wrong buffer
void test_wrong_guard(size_t n) {
    char dest[50];
    char src[100];
    if (n <= sizeof(src)) {  // Wrong: should check dest
        memcpy(dest, src, n);  // ERROR: n could be up to 100
    }
}

// Variant 11.2: guard off-by-one
void test_guard_off_by_one(size_t n) {
    char buf[64];
    if (n <= sizeof(buf) + 1) {  // Wrong: allows n = 65
        memcpy(buf, "x", n);  // ERROR: n could be 65
    }
}

// =============================================================================
// SECTION 12: Integer overflow in size calculation
// =============================================================================

// Variant 12.1: multiplication overflow (future: integer overflow tracking)
void test_int_overflow(size_t count) {
    char buf[1000];
    // count * 100 could overflow, wrapping to small value
    // Then memcpy would use the small value... complex case
    memcpy(buf, "x", count * 100);  // ERROR: unguarded expression
}

// =============================================================================
// SECTION 13: 2D array row overflow
// =============================================================================

// Variant 13.1: memcpy exceeds row size
void test_2d_row_overflow() {
    char arr[10][20];
    memcpy(arr[5], "this string is way too long for 20 bytes", 41);
    // ERROR: 41 > 20 (row capacity)
}

// =============================================================================
// SECTION 14: Accumulation overflow (CVE-2018-18820 adjacent)
// =============================================================================

// Variant 14.1: accumulated offset exceeds buffer
void test_accumulation() {
    char buf[100];
    size_t offset = 0;
    offset += 60;
    offset += 60;  // offset = 120
    memcpy(buf + offset, "x", 1);  // ERROR: offset > buf capacity
}

// Variant 14.2: loop accumulation (simplified)
void test_loop_overflow() {
    char buf[50];
    char* p = buf;
    for (int i = 0; i < 10; ++i) {
        memcpy(p, "hello", 6);  // ERROR on later iterations: p += 6 * 10 > 50
        p += 6;
    }
}

int main() {
    return 0;
}
