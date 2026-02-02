// WHAT IS TESTED:
// Ternary expressions with Z3 ITE (If-Then-Else) evaluation.
// Tests that ternary postconditions can be proven safe when BOTH branches are safe.
//
// EXPECTED: Z3 uses ITE to reason about ternary expressions.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }

    // Both branches are safe (10 > 0 and 5 > 0)
    void set_safe_ternary(bool large) {
        _size = large ? 10 : 5;
    }

    // One branch unsafe (10 > 0 but 0 <= 0)
    void set_unsafe_ternary(bool large) {
        _size = large ? 10 : 0;
    }

    // Both branches >= 10
    void set_large_ternary(bool a) {
        _size = a ? 100 : 10;
    }
};

// Variant 1: Both branches safe for front() (> 0)
void variant1_both_safe() {
    Vec v;
    v._size = 0;
    v.set_safe_ternary(true);  // _size = 10 or 5, both > 0
    v.front();  // OK: ite(cond, 10, 5) > 0 is always true
}

// Variant 2: One branch unsafe - should catch bug
void variant2_one_unsafe() {
    Vec v;
    v._size = 5;
    v.set_unsafe_ternary(false);  // If false, _size = 0
    v.front();  // BUG: could be 0
}

// Variant 3: Both branches satisfy at9() (> 9)
void variant3_both_large() {
    Vec v;
    v._size = 0;
    v.set_large_ternary(false);  // _size = 100 or 10, both > 9
    v.at9();  // OK: ite(cond, 100, 10) > 9 is always true
}

int main() {
    variant1_both_safe();
    variant2_one_unsafe();
    variant3_both_large();
    return 0;
}
