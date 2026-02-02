// WHAT IS TESTED:
// Auto-postcondition inference: DTT should infer postconditions from method bodies.
// This test verifies that inferred postcondition from setToFive() is applied correctly.
//
// EXPECTED: DTT infers postcondition from setToFive() implementation, tracks that
// v._size becomes 5, and allows the subsequent safe access.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 4")]]
    int& at4() { return data[4]; }

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    // NO annotation - DTT should infer ensures=_size == 5 from implementation
    void setToFive() {
        _size = 5;  // <-- DTT should detect this sets _size to 5
    }

    // NO annotation - DTT should infer ensures=_size == arg0 from implementation
    void resize(int n) {
        _size = n;  // <-- DTT should detect this sets _size to n (arg0)
    }
};

// Variant 1: Constant postcondition allows access
void variant1_constant_allows_access() {
    Vec v;
    v._size = 0;
    v.setToFive();  // After this, _size should be inferred as 5
    v.at4();        // OK: _size == 5 > 4
    v.front();      // OK: _size == 5 > 0
}

// Variant 2: Argument postcondition allows access
void variant2_arg_allows_access() {
    Vec v;
    v._size = 0;
    v.resize(10);   // After this, _size should be inferred as 10
    v.at4();        // OK: _size == 10 > 4
}

// Variant 3: Chain of inferred postconditions
void variant3_chained() {
    Vec v;
    v._size = 0;
    v.setToFive();  // _size = 5
    v.front();      // OK: 5 > 0
    v.resize(3);    // _size = 3
    v.front();      // OK: 3 > 0 (but at4() would fail - tested separately)
}

int main() {
    variant1_constant_allows_access();
    variant2_arg_allows_access();
    variant3_chained();
    return 0;
}
