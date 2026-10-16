// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 839_malloc_fail.cpp
// Tests: Unsafe malloc/free usage (DTT catches all variants)

#include <cstdlib>
#include <cstring>

// =============================================================================
// SECTION 1: Dereference without null check
// =============================================================================

// Variant 1.1: dereference malloc result without check
void test_malloc_no_check() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;  // ERROR: malloc might return null  // dtt-error {{null pointer dereference}}
    free(p);
}

// Variant 1.2: dereference calloc result without check
void test_calloc_no_check() {
    int* p = (int*)calloc(10, sizeof(int));
    p[0] = 42;  // KNOWN GAP (not asserted): calloc might return null
    free(p);
}

// Variant 1.3: dereference parameter pointer
void test_param_deref(int* p) {
    *p = 42;  // ERROR: can't prove p is non-null  // dtt-error {{null pointer dereference}}
}

// =============================================================================
// SECTION 2: Use after free
// =============================================================================

// Variant 2.1: use after free
void test_use_after_free() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    free(p);
    *p = 42;  // ERROR: use after free  // dtt-error {{use-after-free}}
}

// Variant 2.2: read after free
void test_read_after_free() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    *p = 42;
    free(p);
    int x = *p;  // ERROR: read after free  // dtt-error {{use-after-free}}
    (void)x;
}

// =============================================================================
// SECTION 3: Double free
// =============================================================================

// Variant 3.1: double free
void test_double_free() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    free(p);
    free(p);  // ERROR: double free  // dtt-error {{double-free}}
}

// Variant 3.2: free then null then free
void test_free_null_free() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    free(p);
    p = nullptr;
    free(p);  // OK: free(null) is safe, but previous free was the problem
}

// =============================================================================
// SECTION 4: Access in wrong branch
// =============================================================================

// Variant 4.1: access when malloc returned null
void test_wrong_branch() {
    int* p = (int*)malloc(sizeof(int));
    if (p) {
        // OK here
    } else {
        *p = 42;  // ERROR: p is null in else branch  // dtt-error {{null pointer dereference}}
    }
    free(p);
}

// =============================================================================
// SECTION 5: Out of bounds access
// =============================================================================

// Variant 5.1: access beyond allocated size
void test_out_of_bounds() {
    int* p = (int*)malloc(10 * sizeof(int));
    if (!p) return;
    p[10] = 42;  // ERROR: index 10 >= allocated 10  // dtt-error {{buffer index.*out of bounds}}
    free(p);
}

// Variant 5.2: access with unknown index
void test_unknown_index(size_t i) {
    int* p = (int*)malloc(10 * sizeof(int));
    if (!p) return;
    p[i] = 42;  // ERROR: can't prove i < 10  // dtt-error {{buffer index.*out of bounds|cannot verify}}
    free(p);
}

// =============================================================================
// SECTION 6: realloc misuse
// =============================================================================

// Variant 6.1: dereference realloc without check
void test_realloc_no_check() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    int* q = (int*)realloc(p, sizeof(int) * 2);
    *q = 42;  // ERROR: realloc might return null  // dtt-error {{null pointer dereference}}
    free(q);
}

// Variant 6.2: use original after realloc succeeds
void test_use_original_after_realloc() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    int* q = (int*)realloc(p, sizeof(int) * 2);
    if (!q) { free(p); return; }
    *p = 42;  // KNOWN GAP (not asserted): p is invalid after successful realloc
    free(q);
}

// =============================================================================
// SECTION 7: Memory leak patterns (not directly catchable but related)
// =============================================================================

// Variant 7.1: overwrite without free (leak)
void test_overwrite_leak() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    p = (int*)malloc(sizeof(int));  // KNOWN GAP (not asserted): original allocation leaked
    if (p) free(p);
}

// =============================================================================
// SECTION 8: memcpy/memset without guard
// =============================================================================

// Variant 8.1: memset on potentially null
void test_memset_no_guard() {
    char* p = (char*)malloc(100);
    memset(p, 0, 100);  // KNOWN GAP (not asserted): p might be null
    free(p);
}

// Variant 8.2: memcpy on potentially null dst
void test_memcpy_no_guard() {
    char* dst = (char*)malloc(100);
    const char* src = "hello";
    memcpy(dst, src, 6);  // KNOWN GAP (not asserted): dst might be null
    free(dst);
}

// =============================================================================
// SECTION 9: Stale pointer check
// =============================================================================

// Variant 9.1: stale null check
void test_stale_check() {
    int* p = (int*)malloc(sizeof(int));
    bool was_valid = (p != nullptr);
    free(p);
    if (was_valid) {
        *p = 42;  // ERROR: stale check, p was freed  // dtt-error {{use-after-free}}
    }
}

// =============================================================================
// SECTION 10: Access moved/transferred pointer
// =============================================================================

// Variant 10.1: access after ownership transfer
void test_transferred_ownership() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    int* q = p;  // Transfer ownership
    free(q);
    *p = 42;  // KNOWN GAP (not asserted): p's memory was freed via q
}

int main() {
    return 0;
}
