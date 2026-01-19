// 839_malloc_path_sensitivity_xfail.cpp
// GAP: DTT lacks path-sensitive analysis for free() in error branches
// Pattern: free() in error branch that returns should not affect code after
// This is a false positive - DTT incorrectly reports use-after-free

#include <cstdlib>

// Variant 1: cleanup on error with early return
// DTT should recognize that free(a) only happens when we return early
int test_cleanup_on_error() {
    int* a = (int*)malloc(sizeof(int));
    if (!a) return -1;

    int* b = (int*)malloc(sizeof(int));
    if (!b) {
        free(a);  // Only in error path that returns
        return -1;
    }

    // GAP: DTT thinks 'a' might be freed here, but it can't be
    // because the free(a) branch returns before reaching here
    *a = 1;  // FALSE POSITIVE: use-after-free
    *b = 2;

    free(a);  // FALSE POSITIVE: double-free
    free(b);
    return 0;
}

int main() { return 0; }
