// 1303_cached_property_desync_xfail.cpp
// Tests: Cached property binding gaps — element count and pointer offset.
// DTT cached property binding currently can't match:
// 1. Element count: _size = Div(Mul(Var("n"), Const(4)), Const(4)) != Var("n")
//    (needs symbolic simplification)
// 2. Pointer offset: saved_off = 5 is a literal, not derived from a tracked property

#include <cstdlib>
#include <cstring>

void process_elements(int* data, size_t count);
void process_at_offset(int* data, size_t offset);

// =============================================================================
// VARIANT 1: Cached element count (_size) goes stale after realloc
// Gap: _size = Div(Mul(Var("n"), Const(4)), Const(4)) doesn't match Var("n")
// =============================================================================
void test_cached_element_count(size_t n) {
    int* arr = (int*)malloc(n * sizeof(int));
    if (!arr) return;
    size_t count = n;              // count caches arr._size (element count)

    // Shrink the array
    int* tmp = (int*)realloc(arr, (n / 2) * sizeof(int));
    if (!tmp) { free(arr); return; }
    arr = tmp;
    // arr._size is now n/2, count is still n — DESYNC!

    process_elements(arr, count);  // BUG: claims n elements, only n/2 exist
    free(arr);
}

// =============================================================================
// VARIANT 2: Cached pointer offset goes stale after pointer reset
// Gap: saved_off = 5 is a constant literal, not derived from tracked property
// =============================================================================
void test_cached_offset(size_t n) {
    int* arr = (int*)malloc(n * sizeof(int));
    if (!arr) return;

    int* p = arr + 5;             // p._offset = 5
    size_t saved_off = 5;         // saved_off caches p._offset

    p = arr + 2;                  // p._offset changes to 2
    // saved_off still 5 — DESYNC!

    process_at_offset(arr, saved_off);  // BUG: uses stale offset
    free(arr);
}

int main() { return 0; }
