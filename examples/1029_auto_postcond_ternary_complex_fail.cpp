// WHAT IS TESTED:
// Complex ternary expression resolution in postcondition inference.
// Tests: nested ternary, ternary with arithmetic, ternary with fields.
//
// EXPECTED: DTT evaluates ternary expressions when condition is known.

struct Vec {
    int* data;
    int _size;
    int _capacity;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }

    [[clang::annotate("dtt:requires=_size >= n")]]
    void require_n(int n) {}

    // Simple ternary with bool
    void set_by_bool(bool large) {
        _size = large ? 10 : 5;
    }

    // Ternary with int condition
    void set_by_int(int x) {
        _size = x ? 10 : 0;
    }

    // Nested ternary
    void set_nested(bool a, bool b) {
        _size = a ? (b ? 20 : 10) : (b ? 5 : 0);
    }

    // Ternary with arithmetic in branches
    void set_ternary_arith(bool flag, int base) {
        _size = flag ? base * 2 : base + 1;
    }

    // Ternary choosing field
    void set_from_choice(bool use_cap) {
        _size = use_cap ? _capacity : 0;
    }

    // Multiple ternaries
    void set_multi_ternary(bool a, bool b) {
        int x = a ? 5 : 3;
        int y = b ? 2 : 1;
        _size = x * y;
    }

    // Ternary in larger expression
    void set_complex_ternary(bool flag, int n) {
        _size = (flag ? n : 0) + 5;
    }

    // Three-way via nested ternary
    void set_three_way(int level) {
        _size = level == 0 ? 0 : (level == 1 ? 5 : 15);
    }
};

// Variant 1: Simple bool true
void variant1_bool_true() {
    Vec v;
    v._size = 0;
    v.set_by_bool(true);  // _size = 10
    v.at9();  // OK: 10 > 9
}

// Variant 2: Simple bool false - unsafe
void variant2_bool_false() {
    Vec v;
    v._size = 0;
    v.set_by_bool(false);  // _size = 5
    v.at9();  // BUG: 5 <= 9  dtt-error {{_size > 9}}
}

// Variant 3: Int condition nonzero
void variant3_int_nonzero() {
    Vec v;
    v._size = 0;
    v.set_by_int(42);  // _size = 10
    v.at9();  // OK: 10 > 9
}

// Variant 4: Int condition zero
void variant4_int_zero() {
    Vec v;
    v._size = 5;
    v.set_by_int(0);  // _size = 0
    v.front();  // BUG: 0 <= 0  dtt-error {{_size > 0}}
}

// Variant 5: Nested both true
void variant5_nested_tt() {
    Vec v;
    v._size = 0;
    v.set_nested(true, true);  // _size = 20
    v.at9();  // OK: 20 > 9
}

// Variant 6: Nested first true, second false
void variant6_nested_tf() {
    Vec v;
    v._size = 0;
    v.set_nested(true, false);  // _size = 10
    v.at9();  // OK: 10 > 9
}

// Variant 7: Nested first false, second true
void variant7_nested_ft() {
    Vec v;
    v._size = 0;
    v.set_nested(false, true);  // _size = 5
    v.at9();  // BUG: 5 <= 9  dtt-error {{_size > 9}}
}

// Variant 8: Nested both false
void variant8_nested_ff() {
    Vec v;
    v._size = 5;
    v.set_nested(false, false);  // _size = 0
    v.front();  // BUG: 0 <= 0  dtt-error {{_size > 0}}
}

// Variant 9: Ternary arithmetic true
void variant9_arith_true() {
    Vec v;
    v._size = 0;
    v.set_ternary_arith(true, 6);  // _size = 6 * 2 = 12
    v.at9();  // OK: 12 > 9
}

// Variant 10: Ternary arithmetic false
void variant10_arith_false() {
    Vec v;
    v._size = 0;
    v.set_ternary_arith(false, 6);  // _size = 6 + 1 = 7
    v.at9();  // BUG: 7 <= 9  dtt-error {{_size > 9}}
}

// Variant 11: Ternary choosing positive capacity
void variant11_choice_cap() {
    Vec v;
    v._capacity = 15;
    v._size = 0;
    v.set_from_choice(true);  // _size = _capacity = 15
    v.at9();  // OK: 15 > 9
}

// Variant 12: Ternary choosing zero
void variant12_choice_zero() {
    Vec v;
    v._capacity = 15;
    v._size = 10;
    v.set_from_choice(false);  // _size = 0
    v.front();  // BUG: 0 <= 0  dtt-error {{_size > 0}}
}

// Variant 13: Multiple ternaries safe
void variant13_multi_safe() {
    Vec v;
    v._size = 0;
    v.set_multi_ternary(true, true);  // x=5, y=2, _size = 10
    v.at9();  // OK: 10 > 9
}

// Variant 14: Multiple ternaries unsafe
void variant14_multi_unsafe() {
    Vec v;
    v._size = 0;
    v.set_multi_ternary(false, false);  // x=3, y=1, _size = 3
    v.at9();  // BUG: 3 <= 9  dtt-error {{_size > 9}}
}

// Variant 15: Complex ternary safe
void variant15_complex_safe() {
    Vec v;
    v._size = 0;
    v.set_complex_ternary(true, 5);  // (5) + 5 = 10
    v.at9();  // OK: 10 > 9
}

// Variant 16: Complex ternary unsafe
void variant16_complex_unsafe() {
    Vec v;
    v._size = 0;
    v.set_complex_ternary(false, 100);  // (0) + 5 = 5
    v.at9();  // BUG: 5 <= 9  dtt-error {{_size > 9}}
}

// Variant 17: Three-way level 2
void variant17_threeway_2() {
    Vec v;
    v._size = 0;
    v.set_three_way(2);  // _size = 15
    v.at9();  // OK: 15 > 9
}

// Variant 18: Three-way level 0
void variant18_threeway_0() {
    Vec v;
    v._size = 5;
    v.set_three_way(0);  // _size = 0
    v.front();  // BUG: 0 <= 0  dtt-error {{_size > 0}}
}

int main() {
    variant1_bool_true();
    variant2_bool_false();
    variant3_int_nonzero();
    variant4_int_zero();
    variant5_nested_tt();
    variant6_nested_tf();
    variant7_nested_ft();
    variant8_nested_ff();
    variant9_arith_true();
    variant10_arith_false();
    variant11_choice_cap();
    variant12_choice_zero();
    variant13_multi_safe();
    variant14_multi_unsafe();
    variant15_complex_safe();
    variant16_complex_unsafe();
    variant17_threeway_2();
    variant18_threeway_0();
    return 0;
}
