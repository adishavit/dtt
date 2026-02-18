// Contract assertion: assert establishes a bound, but access exceeds it.
// dtt_contract_assert(_size > 0) does NOT prove _size > 5.
//
// DTT should still catch out-of-bounds even when a weaker assertion is present.

#include <vector>

static inline void dtt_contract_assert(bool) {}

// Variant 1: Assert non-empty but access index 5 (insufficient bound)
void test_weak_bound() {
    std::vector<int> v;
    dtt_contract_assert(v.size() > 0);    // Only proves _size > 0
    int x = v[5];                         // ERROR: can't prove 5 < _size
    (void)x;
}

// Variant 2: No assertion at all — straightforward empty vector access
void test_no_assert() {
    std::vector<int> v;
    v.front();                            // ERROR: _size == 0
}

// Variant 3: Assert on wrong object doesn't help
void test_wrong_object() {
    std::vector<int> v1, v2;
    dtt_contract_assert(v1.size() > 0);   // Proof on v1, not v2
    v2.front();                           // ERROR: v2._size == 0
}

int main() { return 0; }
