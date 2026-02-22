// 1200_loop_invariant_fail.cpp - Loop invariant inference (not yet implemented)
// Tests that DTT should infer forall properties from loop patterns
//
// Gap: DTT cannot currently infer array properties from initialization loops.
// When implemented, this test should fail (DTT will catch the violation).

#include <cstddef>

[[clang::annotate("dtt:requires=forall(i, 0, n, arr[i] > 0)")]]
void requires_positive(const int* arr, size_t n);

// Gap: After this loop, DTT should know forall(i, 0, n, arr[i] == i + 1)
// which implies forall(i, 0, n, arr[i] > 0) for n > 0
void test_loop_invariant_inference() {
    int arr[10];
    for (int i = 0; i < 10; ++i) {
        arr[i] = i + 1;  // All values 1-10, all positive
    }
    // DTT should infer: forall(i, 0, 10, arr[i] > 0)
    requires_positive(arr, 10);  // XFAIL: DTT cannot prove this yet
}

int main() {
    test_loop_invariant_inference();
    return 0;
}
