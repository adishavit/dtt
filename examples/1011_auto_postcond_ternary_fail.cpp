// WHAT IS TESTED:
// Auto-postcondition inference with ternary operator assignments.
// Gap: DTT may not parse ternary expressions in RHS.
//
// EXPECTED: DTT infers postcondition from ternary expression.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 4")]]
    int& at4() { return data[4]; }

    // Gap: Ternary with constants
    void set_ternary_const(bool large) {
        _size = large ? 10 : 0;
    }

    // Gap: Ternary with argument
    void set_ternary_arg(int n, bool useIt) {
        _size = useIt ? n : 0;
    }

    // Gap: Ternary with old value
    void maybe_increment(bool doIt) {
        _size = doIt ? _size + 1 : _size;
    }

    // Gap: Nested ternary
    void nested_ternary(bool a, bool b) {
        _size = a ? (b ? 10 : 5) : 0;
    }
};

// Variant 1: Ternary selects safe value
void variant1_ternary_safe() {
    Vec v;
    v._size = 0;
    v.set_ternary_const(true);  // _size = 10
    v.front();  // OK: 10 > 0
}

// Variant 2: Ternary selects unsafe value
void variant2_ternary_unsafe() {
    Vec v;
    v._size = 5;
    v.set_ternary_const(false);  // _size = 0
    v.front();  // BUG: _size == 0  dtt-error {{_size > 0}}
}

// Variant 3: Ternary with argument
void variant3_ternary_arg() {
    Vec v;
    v._size = 0;
    v.set_ternary_arg(5, true);  // _size = 5
    v.at4();  // OK: 5 > 4
}

// Variant 4: Ternary increment
void variant4_ternary_increment() {
    Vec v;
    v._size = 0;
    v.maybe_increment(true);  // _size = 1
    v.front();  // OK: 1 > 0
}

// Variant 5: Nested ternary unsafe
void variant5_nested_unsafe() {
    Vec v;
    v._size = 5;
    v.nested_ternary(false, true);  // _size = 0
    v.front();  // BUG: _size == 0  dtt-error {{_size > 0}}
}

int main() {
    variant1_ternary_safe();
    variant2_ternary_unsafe();
    variant3_ternary_arg();
    variant4_ternary_increment();
    variant5_nested_unsafe();
    return 0;
}
