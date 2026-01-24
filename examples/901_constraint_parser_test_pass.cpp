// DTT Phase 2 Constraint Parser Integration Test
// This test verifies that complex constraints are parsed and checked correctly

#include <vector>

// Test container with various constraint patterns
struct TestVec {
    int _size = 0;
    int _capacity = 10;
    int* data = nullptr;

    // Test 1: Simple comparison
    [[clang::annotate("dtt:requires=_size > 0")]]
    int front() { return data[0]; }

    // Test 2: Equality postcondition
    [[clang::annotate("dtt:ensures=_size == 0")]]
    void clear() { _size = 0; }

    // Test 3: old() reference
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(int x) { data[_size++] = x; }

    // Test 4: Arithmetic expression
    [[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
    void pop_back() { _size--; }

    // Test 5: arg vs field
    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int at(int idx) { return data[idx]; }

    // Test 6: Multiple args with arithmetic
    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void copy_range(int start, int count) {}

    // Test 7: Compound AND constraint
    [[clang::annotate("dtt:requires=_size > 0 && arg0 < _size")]]
    int safe_at(int idx) { return data[idx]; }

    // Test 8: Boolean postconditions
    [[clang::annotate("dtt:ensures_if_true=_size == 0")]]
    [[clang::annotate("dtt:ensures_if_false=_size > 0")]]
    bool empty() const { return _size == 0; }

    // Test 9: Nested arithmetic
    [[clang::annotate("dtt:requires=arg0 * 2 + 1 < _size")]]
    int double_plus_one(int idx) { return data[idx * 2 + 1]; }

    // Test 10: Comparison with capacity (const method doesn't invalidate)
    [[clang::annotate("dtt:requires=_size < _capacity")]]
    void ensure_space() const {}
};

int main() {
    TestVec v;
    v._size = 5;
    v._capacity = 10;
    v.data = new int[10];

    // Test simple comparison: _size > 0
    v.front();  // OK: 5 > 0

    // Test arg < field: arg0 < _size
    v.at(2);    // OK: 2 < 5

    // Test arithmetic: arg0 + arg1 <= _size
    v.copy_range(1, 3);  // OK: 1 + 3 = 4 <= 5

    // Test compound: _size > 0 && arg0 < _size
    v.safe_at(3);  // OK: 5 > 0 && 3 < 5

    // Test nested arithmetic: arg0 * 2 + 1 < _size
    v.double_plus_one(1);  // OK: 1 * 2 + 1 = 3 < 5

    // Test capacity comparison
    v.ensure_space();  // OK: 5 < 10

    // Test boolean postcondition with guard
    if (!v.empty()) {
        v.front();    // OK: !empty() proves _size > 0
    }

    // Reset to known state for further tests
    v._size = 5;

    // Test old() postcondition tracking
    // Note: After push_back, _size is tracked symbolically as old+1
    // This is correct behavior - DTT tracks symbolic values

    delete[] v.data;
    return 0;
}
