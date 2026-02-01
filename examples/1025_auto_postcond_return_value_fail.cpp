// WHAT IS TESTED:
// Auto-postcondition inference for return values.
// DTT should infer _result == expr from return statements.
//
// EXPECTED: DTT tracks return values at call sites.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    // Should infer: _result == _size
    int size() const { return _size; }

    // Should infer: _result == _size - 1 (returns index of last element)
    int last_index() const { return _size - 1; }

    // Should infer: _size == old(_size) - 1, _result == old(_size) - 1
    int pop() {
        _size--;
        return _size;
    }

    // Should infer: _size == 0
    void clear() { _size = 0; }
};

[[clang::annotate("dtt:requires=n > 0")]]
void require_positive(int n) {}

[[clang::annotate("dtt:requires=idx >= 0")]]
void require_nonneg(int idx) {}

// Variant 1: Simple return value
void variant1_size_return() {
    Vec v;
    v._size = 10;
    int s = v.size();      // s == 10
    require_positive(s);   // OK: 10 > 0
}

// Variant 2: Return value after clear - should catch bug
void variant2_size_after_clear() {
    Vec v;
    v._size = 10;
    v.clear();             // _size = 0
    int s = v.size();      // s == 0
    require_positive(s);   // BUG: 0 <= 0
}

// Variant 3: Computed return value
void variant3_last_index() {
    Vec v;
    v._size = 5;
    int idx = v.last_index();  // idx == 4
    require_nonneg(idx);       // OK: 4 >= 0
}

// Variant 4: Return with side effect
void variant4_pop_return() {
    Vec v;
    v._size = 5;
    int remaining = v.pop();   // _size = 4, remaining = 4
    require_positive(remaining); // OK: 4 > 0
}

// Variant 5: Pop to zero - should catch bug
void variant5_pop_to_zero() {
    Vec v;
    v._size = 1;
    int remaining = v.pop();   // _size = 0, remaining = 0
    require_positive(remaining); // BUG: 0 <= 0
}

int main() {
    variant1_size_return();
    variant2_size_after_clear();
    variant3_last_index();
    variant4_pop_return();
    variant5_pop_to_zero();
    return 0;
}
