// Test: VarBoundProofs must be invalidated when the referenced field changes
// Bug: if (i < v.size()) { v.pop_back(); v[i]; } - proof "i < _size" survived size decrease
// Expected: FAIL - after pop_back decreases size, the old bound proof is invalid
// Example: i=9, size=10 → i < size. After pop_back: size=9, but i=9 is NOT < 9

#include <vector>

void test(int i) {
    std::vector<int> v;
    v.resize(10);  // _size = 10

    if (i < v.size()) {
        // Here: proof that i < v._size (which is 10), so i could be 0-9
        v.pop_back();  // _size = 9, invalidates "i < v._size" proof

        // This should FAIL: i could be 9, which is NOT < 9
        // The proof was invalidated when _size changed
        v[i];  // ERROR: bound proof was invalidated by pop_back  dtt-error {{i < 9}}
    }
}
