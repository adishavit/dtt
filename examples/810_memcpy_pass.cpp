// 810_memcpy_pass.cpp
// Tests: Safe memcpy/memmove/memset usage that should compile without DTT errors
// Reference: PHASE1_C_MEMORY_FUNCTIONS.md

#include <cstring>
#include <cstddef>

// =============================================================================
// SECTION 1: Basic safe memcpy with known sizes
// =============================================================================

// Variant 1.1: memcpy within buffer bounds
void test_memcpy_basic_safe() {
    char dest[100];
    char src[50];
    memcpy(dest, src, 50);  // OK: 50 <= 100, 50 <= 50
}

// Variant 1.2: memcpy with sizeof
void test_memcpy_sizeof() {
    char dest[100];
    char src[100];
    memcpy(dest, src, sizeof(src));  // OK: 100 <= 100
}

// Variant 1.3: memcpy smaller than both buffers
void test_memcpy_smaller() {
    char dest[100];
    char src[100];
    memcpy(dest, src, 10);  // OK: 10 <= 100
}

// Variant 1.4: memcpy with explicit size
void test_memcpy_explicit() {
    int dest[10];
    int src[10];
    memcpy(dest, src, 10 * sizeof(int));  // OK: 40 <= 40
}

// =============================================================================
// SECTION 2: Guarded memcpy
// =============================================================================

// Variant 2.1: guarded with if (CVE-2023-38545 fix pattern)
void test_memcpy_guarded(const char* src, size_t src_len) {
    char dest[256];
    if (src_len <= sizeof(dest)) {
        memcpy(dest, src, src_len);  // OK: guarded
    }
}

// Variant 2.2: guarded with min
void test_memcpy_min(const char* src, size_t len) {
    char dest[100];
    size_t copy_len = len < sizeof(dest) ? len : sizeof(dest);
    memcpy(dest, src, copy_len);  // OK: copy_len <= 100
}

// Variant 2.3: guarded with assert-like check
void test_memcpy_assert(const char* src, size_t len) {
    char dest[64];
    if (len > sizeof(dest)) return;
    memcpy(dest, src, len);  // OK: early return guards
}

// =============================================================================
// SECTION 3: Pointer arithmetic (safe patterns)
// =============================================================================

// Variant 3.1: pointer arithmetic with correct offset
void test_memcpy_ptr_arith() {
    char buf[100];
    char* p = buf + 10;
    memcpy(p, "data", 5);  // OK: 5 <= 90 (remaining capacity)
}

// Variant 3.2: pointer arithmetic with guard
void test_memcpy_ptr_guard(size_t offset) {
    char buf[100];
    if (offset + 10 <= sizeof(buf)) {
        memcpy(buf + offset, "0123456789", 10);  // OK: guarded
    }
}

// Variant 3.3: nested pointer arithmetic
void test_memcpy_nested_ptr() {
    char buf[100];
    char* p = buf + 20;
    char* q = p + 30;
    memcpy(q, "hi", 3);  // OK: 3 <= 50 (100 - 20 - 30)
}

// =============================================================================
// SECTION 4: memmove (same rules as memcpy)
// =============================================================================

// Variant 4.1: memmove basic
void test_memmove_basic() {
    char buf[100];
    memmove(buf + 10, buf, 50);  // OK: overlapping is fine, 50 <= 90
}

// Variant 4.2: memmove with sizeof
void test_memmove_sizeof() {
    int arr[10];
    memmove(arr + 2, arr, 8 * sizeof(int));  // OK: 32 <= 32
}

// =============================================================================
// SECTION 5: memset
// =============================================================================

// Variant 5.1: memset entire buffer
void test_memset_entire() {
    char buf[100];
    memset(buf, 0, sizeof(buf));  // OK: 100 <= 100
}

// Variant 5.2: memset partial buffer
void test_memset_partial() {
    char buf[100];
    memset(buf, 'x', 50);  // OK: 50 <= 100
}

// Variant 5.3: memset with pointer arithmetic
void test_memset_ptr_arith() {
    char buf[100];
    memset(buf + 10, 0, 90);  // OK: 90 <= 90
}

// Variant 5.4: memset guarded
void test_memset_guarded(size_t n) {
    char buf[64];
    if (n <= sizeof(buf)) {
        memset(buf, 0, n);  // OK: guarded
    }
}

// =============================================================================
// SECTION 6: memcmp
// =============================================================================

// Variant 6.1: memcmp within bounds
void test_memcmp_basic() {
    char a[10] = "hello";
    char b[10] = "world";
    memcmp(a, b, 5);  // OK: 5 <= 10
}

// Variant 6.2: memcmp with sizeof
void test_memcmp_sizeof() {
    int a[5], b[5];
    memcmp(a, b, sizeof(a));  // OK: 20 <= 20
}

// =============================================================================
// SECTION 7: memchr
// =============================================================================

// Variant 7.1: memchr within bounds
void test_memchr_basic() {
    char buf[100] = "hello world";
    memchr(buf, 'w', 20);  // OK: 20 <= 100
}

// Variant 7.2: memchr with sizeof
void test_memchr_sizeof() {
    char buf[50];
    memchr(buf, '\0', sizeof(buf));  // OK: 50 <= 50
}

// =============================================================================
// SECTION 8: Struct buffers
// =============================================================================

// Variant 8.1: memcpy struct to struct
void test_memcpy_struct() {
    struct S { int a; char b[10]; };
    S dest, src;
    memcpy(&dest, &src, sizeof(S));  // OK
}

// Variant 8.2: memset struct to zero
void test_memset_struct() {
    struct S { int a; char b[10]; };
    S s;
    memset(&s, 0, sizeof(s));  // OK
}

// =============================================================================
// SECTION 9: Symbolic size with proof
// =============================================================================

// Variant 9.1: symbolic n with guard
void test_symbolic_guard(size_t n) {
    char buf[100];
    if (n <= 100) {
        memcpy(buf, "x", n);  // OK: n <= 100 proven
    }
}

// Variant 9.2: symbolic with early return
void test_symbolic_early_return(size_t n) {
    char buf[64];
    if (n > sizeof(buf)) return;
    memset(buf, 0, n);  // OK: n <= 64 proven
}

// =============================================================================
// SECTION 10: VLA-like patterns (fixed size at declaration)
// =============================================================================

// Variant 10.1: array size from constant
void test_const_size() {
    const size_t SIZE = 128;
    char buf[SIZE];
    memcpy(buf, "test", 5);  // OK: 5 <= 128
}

// Variant 10.2: array size from constexpr
void test_constexpr_size() {
    constexpr size_t SIZE = 64;
    char buf[SIZE];
    memset(buf, 'a', SIZE);  // OK: 64 <= 64
}

// =============================================================================
// SECTION 11: Multiple operations
// =============================================================================

// Variant 11.1: sequential memcpy calls
void test_sequential() {
    char buf[100];
    memcpy(buf, "hello", 5);      // OK: 5 <= 100
    memcpy(buf + 5, " ", 1);      // OK: 1 <= 95
    memcpy(buf + 6, "world", 5);  // OK: 5 <= 94
}

// Variant 11.2: memset then memcpy
void test_set_then_copy() {
    char buf[50];
    memset(buf, 0, sizeof(buf));  // OK
    memcpy(buf, "init", 4);       // OK: 4 <= 50
}

// =============================================================================
// SECTION 12: 2D arrays
// =============================================================================

// Variant 12.1: 2D array memcpy
void test_2d_array() {
    char arr[10][20];
    char row[20];
    memcpy(arr[5], row, sizeof(row));  // OK: 20 <= 20
}

// Variant 12.2: 2D array memset row
void test_2d_memset() {
    int matrix[5][5];
    memset(matrix[0], 0, 5 * sizeof(int));  // OK: 20 <= 20
}

int main() {
    return 0;
}
