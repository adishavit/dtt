// Value Concepts: Bounded forall quantifier
// This test documents the gap: DTT should catch these but doesn't yet
//
// When forall is implemented:
// - Rename to 1000_forall_basic_fail.cpp
// - These should produce DTT errors

#include <cstddef>

// Variant 1: Function requires all elements positive
// forall(i, 0, n, arr[i] > 0) is not yet supported
[[clang::annotate("dtt:requires=forall(i, 0, arg1, arg0[i] > 0)")]]
void process_positive(int* arr, size_t n) {
    // Would use arr knowing all elements > 0
}

// Variant 2: Function requires all elements in range
[[clang::annotate("dtt:requires=forall(i, 0, arg1, arg0[i] >= 0 && arg0[i] < 256)")]]
void process_bytes(int* arr, size_t n) {
    // Would use arr knowing all elements are valid bytes
}

// Variant 3: Function requires sorted array
[[clang::annotate("dtt:requires=forall(i, 0, arg1 - 1, arg0[i] <= arg0[i + 1])")]]
int binary_search(int* arr, size_t n, int target) {
    // Binary search requires sorted input
    return -1;
}

int main() {
    int mixed[] = {1, -2, 3, 4, 5};  // Has negative!
    int big[] = {100, 200, 300};     // Values > 256!
    int unsorted[] = {5, 2, 8, 1};   // Not sorted!

    // All of these should fail when forall is implemented:
    process_positive(mixed, 5);      // SHOULD FAIL: mixed[1] = -2, not > 0
    process_bytes(big, 3);           // SHOULD FAIL: big[1] = 200 is ok, but we can't prove it
    binary_search(unsorted, 4, 3);   // SHOULD FAIL: array not sorted

    return 0;
}
