// WHAT IS TESTED:
// Auto-postcondition inference for field-to-field assignments.
// Gap: DTT may not track _size = _capacity style assignments.
//
// EXPECTED: DTT infers ensures=_size == _capacity (or similar relation).

struct Vec {
    int* data;
    int _size;
    int _capacity;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size == _capacity")]]
    void require_full() { }

    // Gap: Copy one field to another
    void fill_to_capacity() {
        _size = _capacity;
    }

    // Gap: Swap fields
    void swap_size_cap() {
        int tmp = _size;
        _size = _capacity;
        _capacity = tmp;
    }

    // Gap: Field arithmetic
    void double_from_cap() {
        _size = _capacity * 2;
    }

    // Gap: Min/max of fields
    void clamp_to_cap() {
        if (_size > _capacity) {
            _size = _capacity;
        }
    }
};

// Variant 1: Fill to capacity
void variant1_fill() {
    Vec v;
    v._size = 0;
    v._capacity = 10;
    v.fill_to_capacity();
    v.front();  // OK: _size == 10 > 0
}

// Variant 2: Fill when capacity is 0
void variant2_fill_zero_cap() {
    Vec v;
    v._size = 5;
    v._capacity = 0;
    v.fill_to_capacity();
    v.front();  // BUG: _size == 0  dtt-error {{_size > 0}}
}

// Variant 3: Require full after fill
void variant3_require_full() {
    Vec v;
    v._size = 5;
    v._capacity = 10;
    v.fill_to_capacity();
    v.require_full();  // OK: _size == _capacity == 10
}

// Variant 4: Double from cap
void variant4_double() {
    Vec v;
    v._size = 0;
    v._capacity = 5;
    v.double_from_cap();
    v.front();  // OK: _size == 10 > 0
}

// Variant 5: Swap makes size zero
void variant5_swap_zero() {
    Vec v;
    v._size = 5;
    v._capacity = 0;
    v.swap_size_cap();  // Now _size = 0, _capacity = 5
    v.front();  // BUG: _size == 0  dtt-error {{_size > 0}}
}

int main() {
    variant1_fill();
    variant2_fill_zero_cap();
    variant3_require_full();
    variant4_double();
    variant5_swap_zero();
    return 0;
}
