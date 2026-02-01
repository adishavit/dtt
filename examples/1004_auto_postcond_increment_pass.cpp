// WHAT IS TESTED:
// Auto-postcondition inference: DTT should infer postconditions from method bodies.
// This test: _size = _size + 1 (increment) should infer ensures=_size == old(_size) + 1
//
// EXPECTED: DTT infers postcondition from push() implementation, tracks that
// v._size increments, and allows the subsequent safe access.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    // NO annotation - DTT should infer ensures=_size == old(_size) + 1
    void push() {
        _size = _size + 1;
    }

    // NO annotation - DTT should infer ensures=_size == old(_size) - 1
    void pop() {
        _size = _size - 1;
    }
};

// Variant 1: Single increment allows access
void variant1_single_increment() {
    Vec v;
    v._size = 1;
    v.push();     // _size = 1 + 1 = 2
    v.at1();      // OK: _size == 2 > 1
}

// Variant 2: Multiple increments
void variant2_multiple_increments() {
    Vec v;
    v._size = 0;
    v.push();     // _size = 0 + 1 = 1
    v.front();    // OK: _size == 1 > 0
    v.push();     // _size = 1 + 1 = 2
    v.at1();      // OK: _size == 2 > 1
}

// Variant 3: Increment and decrement tracking
void variant3_increment_decrement() {
    Vec v;
    v._size = 2;
    v.push();     // _size = 2 + 1 = 3
    v.at1();      // OK: _size == 3 > 1
    v.pop();      // _size = 3 - 1 = 2
    v.at1();      // OK: _size == 2 > 1
}

int main() {
    variant1_single_increment();
    variant2_multiple_increments();
    variant3_increment_decrement();
    return 0;
}
