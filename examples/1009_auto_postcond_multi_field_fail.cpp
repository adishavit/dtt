// WHAT IS TESTED:
// Auto-postcondition inference for multiple field modifications in one function.
//
// EXPECTED: DTT infers postconditions for ALL modified fields.
// RESULT: DTT correctly catches bugs for all modified fields.

struct Vec {
    int* data;
    int _size;
    int _capacity;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_capacity > 0")]]
    void reserve_check() { /* uses capacity */ }

    // Gap: Does DTT infer BOTH _size == 0 AND _capacity == 0?
    void clear_all() {
        _size = 0;
        _capacity = 0;
    }

    // Gap: Mixed - constant and argument
    void reset(int cap) {
        _size = 0;
        _capacity = cap;
    }
};

// Variant 1: Both fields cleared
void variant1_both_cleared() {
    Vec v;
    v._size = 5;
    v._capacity = 10;
    v.clear_all();
    // After: _size == 0, _capacity == 0
    // BUG if DTT only tracks one field
}

// Variant 2: Check second field after clear
void variant2_check_second_field() {
    Vec v;
    v._size = 5;
    v._capacity = 10;
    v.clear_all();
    v.reserve_check();  // BUG: _capacity == 0, violates requires > 0
}

// Variant 3: Mixed assignment types
void variant3_mixed() {
    Vec v;
    v._size = 5;
    v._capacity = 0;
    v.reset(20);
    // _size == 0, _capacity == 20
    v.reserve_check();  // OK: _capacity == 20 > 0
}

// Variant 4: Order matters - check first field
void variant4_first_field() {
    Vec v;
    v._size = 5;
    v._capacity = 10;
    v.clear_all();
    v.front();  // BUG: _size == 0, violates requires > 0
}

int main() {
    variant1_both_cleared();
    variant2_check_second_field();
    variant3_mixed();
    variant4_first_field();
    return 0;
}
