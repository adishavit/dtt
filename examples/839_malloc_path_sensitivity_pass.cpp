// 839_malloc_path_sensitivity_pass.cpp
// Tests: Path-sensitive lifetime tracking for deallocation in early-return branches
// Pattern: dealloc in error branch that returns does not affect code after

#include <cstdlib>
#include <new>

// =============================================================================
// SECTION 1: free() in early return
// =============================================================================

// Variant 1.1: cleanup with free() on error
int test_free_on_error() {
    int* a = (int*)malloc(sizeof(int));
    if (!a) return -1;

    int* b = (int*)malloc(sizeof(int));
    if (!b) {
        free(a);  // Only in error path that returns
        return -1;
    }

    // DTT understands 'a' is NOT freed here because the
    // free(a) branch returns before reaching this point
    *a = 1;  // OK: path-sensitive analysis
    *b = 2;

    free(a);  // OK: first free on this path
    free(b);
    return 0;
}

// =============================================================================
// SECTION 2: delete in early return
// =============================================================================

// Variant 2.1: cleanup with delete on error
int test_delete_on_error() {
    int* a = new int;
    int* b = new (std::nothrow) int;
    if (!b) {
        delete a;  // Only in error path that returns
        return -1;
    }

    *a = 1;  // OK: path-sensitive analysis
    *b = 2;

    delete a;  // OK: first delete on this path
    delete b;
    return 0;
}

// =============================================================================
// SECTION 3: delete[] in early return
// =============================================================================

// Variant 3.1: cleanup with delete[] on error
int test_delete_array_on_error() {
    int* a = new int[10];
    int* b = new (std::nothrow) int[10];
    if (!b) {
        delete[] a;  // Only in error path that returns
        return -1;
    }

    a[0] = 1;  // OK: path-sensitive analysis
    b[0] = 2;

    delete[] a;  // OK: first delete[] on this path
    delete[] b;
    return 0;
}

// =============================================================================
// SECTION 4: else-return pattern
// =============================================================================

// Variant 4.1: dealloc in else branch that returns
int test_else_return() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return -1;

    bool success = true;  // Some condition
    if (success) {
        *p = 42;  // OK: p still valid
    } else {
        free(p);  // Only in else that returns
        return -1;
    }

    // p still valid because else branch returned
    *p = 100;  // OK: path-sensitive analysis
    free(p);
    return 0;
}

int main() { return 0; }
