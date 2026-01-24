// 716_negative_index_fail.cpp
// Test: Negative indices should always be rejected
// Expected: DTT errors for all negative index accesses

#include <vector>

void test_negative_literal() {
    std::vector<int> v;
    v.resize(5);
    v[-1];    // ERROR: negative index
}

void test_negative_literal_large() {
    std::vector<int> v;
    v.resize(1000);
    v[-1];    // ERROR: negative index (even with large size)
}

void test_negative_literal_minus_two() {
    std::vector<int> v;
    v.resize(5);
    v[-2];    // ERROR: negative index
}

// Note: Variable negative index (int idx = -1; v[idx]) requires
// constant propagation tracking - not yet supported for this case

int main() {
    return 0;
}
