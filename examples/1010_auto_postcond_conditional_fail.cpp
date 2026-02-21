// WHAT IS TESTED:
// Auto-postcondition inference with conditional (if/else) modifications.
// Gap: DTT likely doesn't handle path-sensitive postconditions.
//
// EXPECTED: DTT should either:
// a) Infer conditional postcondition: ensures=(cond && _size==0) || (!cond && _size==old)
// b) Or conservatively: ensures=_size==0 || _size==old(_size)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    // Gap: Conditional modification
    void maybe_clear(bool doClear) {
        if (doClear) {
            _size = 0;
        }
        // else: _size unchanged
    }

    // Gap: If-else with different values
    void set_size(bool large) {
        if (large) {
            _size = 100;
        } else {
            _size = 1;
        }
    }

    // Gap: Nested conditionals
    void complex_set(bool a, bool b) {
        if (a) {
            if (b) {
                _size = 10;
            } else {
                _size = 5;
            }
        } else {
            _size = 0;
        }
    }
};

// Variant 1: Conditional clear with true - should be bug
void variant1_clear_true() {
    Vec v;
    v._size = 5;
    v.maybe_clear(true);
    v.front();  // BUG: _size == 0
}

// Variant 2: Conditional clear with false - should be safe
void variant2_clear_false() {
    Vec v;
    v._size = 5;
    v.maybe_clear(false);
    v.front();  // OK: _size == 5 (unchanged)
}

// Variant 3: Both branches safe
void variant3_both_safe() {
    Vec v;
    v._size = 0;
    v.set_size(true);   // _size = 100 or 1, both > 0
    v.front();          // OK
}

// Variant 4: One branch unsafe
void variant4_one_unsafe() {
    Vec v;
    v._size = 5;
    v.complex_set(false, true);  // _size = 0
    v.front();  // BUG: _size == 0
}

int main() {
    variant1_clear_true();
    variant2_clear_false();
    variant3_both_safe();
    variant4_one_unsafe();
    return 0;
}
