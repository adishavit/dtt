// WHAT IS TESTED:
// Auto-postcondition inference for unary increment/decrement operators.
// Patterns: _size++, ++_size, _size--, --_size
//
// EXPECTED: DTT infers postconditions from unary operators on 'this' fields
// and applies them at call sites to allow safe access.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }

    // NO annotation - DTT should infer ensures=_size == old(_size) + 1
    void push_postfix() { _size++; }

    // NO annotation - DTT should infer ensures=_size == old(_size) + 1
    void push_prefix() { ++_size; }

    // NO annotation - DTT should infer ensures=_size == old(_size) - 1
    void pop_postfix() { _size--; }

    // NO annotation - DTT should infer ensures=_size == old(_size) - 1
    void pop_prefix() { --_size; }
};

// Variant 1: Postfix increment
void variant1_postfix_increment() {
    Vec v;
    v._size = 0;
    v.push_postfix();  // _size = 0 + 1 = 1
    v.front();         // OK: _size == 1 > 0
}

// Variant 2: Prefix increment
void variant2_prefix_increment() {
    Vec v;
    v._size = 0;
    v.push_prefix();   // _size = 0 + 1 = 1
    v.front();         // OK: _size == 1 > 0
}

// Variant 3: Multiple increments
void variant3_multiple_increments() {
    Vec v;
    v._size = 0;
    v.push_postfix();  // _size = 1
    v.push_prefix();   // _size = 2
    v.at1();           // OK: _size == 2 > 1
}

// Variant 4: Increment and decrement balance
void variant4_balanced() {
    Vec v;
    v._size = 2;
    v.push_postfix();  // _size = 3
    v.pop_prefix();    // _size = 2
    v.at1();           // OK: _size == 2 > 1
}

int main() {
    variant1_postfix_increment();
    variant2_prefix_increment();
    variant3_multiple_increments();
    variant4_balanced();
    return 0;
}
