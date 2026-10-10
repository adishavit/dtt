// 1201_parameterized_concept_fail.cpp - Parameterized concepts (not yet implemented)
// Tests that DTT should support parameterized concept definitions
//
// Gap: DTT cannot currently define concepts with parameters like all_in_range(lo, hi).
// This means users must write full forall expressions instead of reusable concepts.
// When implemented, this syntax will work and the test will pass.

#include <cstddef>

// Gap: This parameterized concept syntax is not yet supported
// The definition parses but parameters aren't substituted
[[clang::annotate("dtt:define=all_in_range(lo, hi) := forall(i, 0, n, lo <= arr[i] && arr[i] < hi)")]] // dtt-error {{invalid concept name}}

// Attempting to use the parameterized concept
[[clang::annotate("dtt:requires=all_in_range(0, 256)")]]
void process_bytes(const int* arr, size_t n);

void test_parameterized_concept() {
    int valid[] = {0, 127, 255};
    // XFAIL: DTT cannot expand all_in_range(0, 256) with parameter substitution
    // The concept definition works but parameters aren't replaced
    process_bytes(valid, 3);
}

int main() {
    test_parameterized_concept();
    return 0;
}
