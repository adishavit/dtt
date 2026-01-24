// Test: VarBoundProof invalidation is selective - only affects the mutated object/field
// Mutating v1._size should NOT invalidate proofs about v2._size
// Expected: PASS - v2's proof survives v1's mutation

#include <vector>

void test(int i) {
    std::vector<int> v1, v2;
    v1.resize(10);  // v1._size = 10
    v2.resize(10);  // v2._size = 10

    if (i < v2.size()) {
        // Proof: i < v2._size (10)

        v1.pop_back();  // v1._size = 9, but v2._size still 10

        // v2's proof should still be valid
        v2[i];  // OK: proof that i < v2._size was NOT invalidated
    }
}
