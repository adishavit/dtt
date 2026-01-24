// Test: VarBoundProofs can be re-established after field changes
// After size mutation, a new guard can prove new bounds
// Expected: PASS - re-checking bounds after mutation is valid

#include <vector>

void test(int i) {
    std::vector<int> v;
    v.resize(10);  // _size = 10

    if (i < v.size()) {
        // Here: proof that i < v._size (which is 10)
        v.pop_back();  // _size = 9, invalidates old proof

        // Re-establish the bound with a new check
        if (i < v.size()) {
            v[i];  // OK: fresh proof that i < 9
        }
    }
}
