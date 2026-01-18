// 838_malloc_pass.cpp
// Tests: Safe malloc/free usage that should compile without DTT errors

#include <cstdlib>
#include <cstring>

// =============================================================================
// SECTION 1: Guard with nullptr check before use
// =============================================================================

// Variant 1.1: if (ptr != nullptr) guard
void test_malloc_null_check() {
    int* p = (int*)malloc(sizeof(int));
    if (p != nullptr) {
        *p = 42;  // OK: guarded
        free(p);
    }
}

// Variant 1.2: if (ptr) guard
void test_malloc_bool_check() {
    int* p = (int*)malloc(sizeof(int));
    if (p) {
        *p = 42;  // OK: guarded
        free(p);
    }
}

// Variant 1.3: early return on null
void test_malloc_early_return() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    *p = 42;  // OK: guarded
    free(p);
}

// =============================================================================
// SECTION 2: calloc with guard
// =============================================================================

// Variant 2.1: calloc with null check
void test_calloc_guard() {
    int* p = (int*)calloc(10, sizeof(int));
    if (p) {
        p[0] = 42;  // OK: guarded
        free(p);
    }
}

// =============================================================================
// SECTION 3: realloc with guard
// =============================================================================

// Variant 3.1: realloc with null check
void test_realloc_guard() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    int* q = (int*)realloc(p, sizeof(int) * 2);
    if (q) {
        q[0] = 42;  // OK: guarded
        free(q);
    } else {
        free(p);  // Original pointer still valid if realloc fails
    }
}

// =============================================================================
// SECTION 4: free with nullptr is safe
// =============================================================================

// Variant 4.1: free(nullptr) is no-op
void test_free_nullptr() {
    int* p = nullptr;
    free(p);  // OK: free(NULL) is defined
}

// Variant 4.2: conditional free
void test_conditional_free() {
    int* p = (int*)malloc(sizeof(int));
    // ... some code that might fail ...
    free(p);  // OK even if p is null
}

// =============================================================================
// SECTION 5: Bounded access after malloc
// =============================================================================

// Variant 5.1: access within allocated size
void test_bounded_access() {
    int* p = (int*)malloc(10 * sizeof(int));
    if (p) {
        for (int i = 0; i < 10; ++i) {
            p[i] = i;  // OK: within bounds
        }
        free(p);
    }
}

// =============================================================================
// SECTION 6: memcpy/memset after guard
// =============================================================================

// Variant 6.1: memset after null check
void test_memset_guard() {
    char* p = (char*)malloc(100);
    if (p) {
        memset(p, 0, 100);  // OK: guarded
        free(p);
    }
}

// Variant 6.2: memcpy after null check
void test_memcpy_guard() {
    char* dst = (char*)malloc(100);
    const char* src = "hello";
    if (dst) {
        memcpy(dst, src, 6);  // OK: guarded
        free(dst);
    }
}

// =============================================================================
// SECTION 7: aligned_alloc with guard
// =============================================================================

// Variant 7.1: aligned_alloc with null check
void test_aligned_alloc_guard() {
    void* p = aligned_alloc(16, 64);
    if (p) {
        // OK: guarded
        free(p);
    }
}

// =============================================================================
// SECTION 8: Store size and use correctly
// =============================================================================

// Variant 8.1: track size with allocation
void test_tracked_size() {
    size_t size = 100;
    int* p = (int*)malloc(size * sizeof(int));
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            p[i] = (int)i;  // OK: within tracked size
        }
        free(p);
    }
}

// =============================================================================
// SECTION 9: Proper cleanup in error paths
// =============================================================================

// Variant 9.1: cleanup on error
int test_cleanup_on_error() {
    int* a = (int*)malloc(sizeof(int));
    if (!a) return -1;

    int* b = (int*)malloc(sizeof(int));
    if (!b) {
        free(a);  // OK: cleanup
        return -1;
    }

    *a = 1;  // OK: both allocated
    *b = 2;  // OK: both allocated

    free(a);
    free(b);
    return 0;
}

// =============================================================================
// SECTION 10: Check before realloc
// =============================================================================

// Variant 10.1: check original before realloc
void test_realloc_pattern() {
    int* p = (int*)malloc(sizeof(int) * 4);
    if (!p) return;

    p[0] = 1;  // OK: guarded

    int* newp = (int*)realloc(p, sizeof(int) * 8);
    if (newp) {
        p = newp;
        p[4] = 5;  // OK: expanded
    }
    // p is still valid (original or new)
    free(p);
}

int main() {
    return 0;
}
