// WHAT IS TESTED:
// Auto-postcondition inference: DTT infers postconditions from method bodies.
// This test: _size = _size + 1 (increment) infers ensures=_size == old(_size) + 1
//
// EXPECTED: DTT infers postcondition from push() implementation, tracks that
// v._size increments, and catches the subsequent unsafe access.
// RESULT: DTT correctly catches the bug.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }

    // NO annotation - DTT should infer ensures=_size == old(_size) + 1 from implementation
    void push() {
        _size = _size + 1;  // <-- DTT should detect this increments _size
    }
};

void test_auto_postcond_increment() {
    Vec v;
    v._size = 0;
    v.push();     // After this, _size should be inferred as 0 + 1 = 1
    v.at1();      // BUG: _size == 1, violates requires=_size > 1
}

int main() {
    test_auto_postcond_increment();
    return 0;
}
