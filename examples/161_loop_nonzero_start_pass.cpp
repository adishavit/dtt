// Test: Loop with non-zero start should track lower bound
// Pattern: for (int i = 1; i < v.size(); i++)
// Should prove: i >= 1 (to make i-1 >= 0 valid for index check)
// Expected: PASS (tests if lower bound tracking works for non-zero start)

#include "../include/dtt_vector.h"

[[clang::annotate("dtt:requires=arg0 >= 1")]]
void needsAtLeastOne(int x);

void test() {
    dtt::vector<int> v;
    v.resize(10);  // _size = 10

    // Loop starting at 1
    for (int i = 1; i < v.size(); i++) {
        needsAtLeastOne(i);  // Requires proof that i >= 1
    }
}
