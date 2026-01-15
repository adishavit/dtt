// DTT Phase 2 Constraint Parser Fail Test
// These calls should produce DTT errors - proving constraints are checked

struct TestVec {
    int _size = 0;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int front() { return 0; }

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int at(int idx) { return 0; }

    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void range(int start, int len) {}

    [[clang::annotate("dtt:requires=arg0 * 2 + 1 < _size")]]
    int double_idx(int idx) { return 0; }
};

// Variant 1: arg < field fails
void test_simple_bound() {
    TestVec v;
    v._size = 3;
    v.at(5);  // ERROR: 5 >= 3
}

// Variant 2: empty container access
void test_empty_access() {
    TestVec v;
    v._size = 0;
    v.front();  // ERROR: 0 is not > 0
}

// Variant 3: arithmetic constraint fails
void test_arithmetic_bound() {
    TestVec v;
    v._size = 5;
    v.range(3, 4);  // ERROR: 3 + 4 = 7 > 5
}

// Variant 4: complex arithmetic fails
void test_complex_arithmetic() {
    TestVec v;
    v._size = 5;
    v.double_idx(2);  // ERROR: 2 * 2 + 1 = 5 is not < 5
}

int main() {
    // All functions above contain errors, but we only call one
    // to get a single clear error message
    test_simple_bound();
    return 0;
}
