// WHAT IS TESTED:
// Field-to-field assignments in postcondition inference.
// Tests: _size = _capacity creates a relationship between fields.
//
// EXPECTED: DTT tracks that _size equals _capacity after assignment.

struct Vec {
    int* data;
    int _size;
    int _capacity;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }

    // Field-to-field: _size = _capacity
    void fill_to_capacity() {
        _size = _capacity;
    }

    // Field arithmetic: _size = _capacity * 2
    void double_capacity() {
        _size = _capacity * 2;
    }
};

// Variant 1: Capacity is positive, so size will be positive
void variant1_positive_cap() {
    Vec v;
    v._size = 0;
    v._capacity = 10;
    v.fill_to_capacity();  // _size = _capacity = 10
    v.front();  // OK: 10 > 0
}

// Variant 2: Capacity is zero, size becomes zero
void variant2_zero_cap() {
    Vec v;
    v._size = 5;
    v._capacity = 0;
    v.fill_to_capacity();  // _size = _capacity = 0
    v.front();  // BUG: 0 <= 0  dtt-error {{_size > 0}}
}

// Variant 3: Double capacity
void variant3_double() {
    Vec v;
    v._size = 0;
    v._capacity = 5;
    v.double_capacity();  // _size = 5 * 2 = 10
    v.at9();  // OK: 10 > 9
}

int main() {
    variant1_positive_cap();
    variant2_zero_cap();
    variant3_double();
    return 0;
}
