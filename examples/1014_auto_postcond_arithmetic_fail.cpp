// WHAT IS TESTED:
// Auto-postcondition inference with complex arithmetic expressions.
// Gap: DTT may only handle simple +/- patterns, not full arithmetic.
//
// EXPECTED: DTT builds symbolic expressions for arbitrary arithmetic.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }

    // Gap: Multiplication
    void double_size() {
        _size = _size * 2;
    }

    // Gap: Division
    void halve_size() {
        _size = _size / 2;
    }

    // Gap: Modulo
    void mod_size(int m) {
        _size = _size % m;
    }

    // Gap: Complex expression
    void complex_expr(int a, int b) {
        _size = (a + b) * 2 - 1;
    }

    // Gap: Bitwise AND (often used for alignment)
    void align_down(int mask) {
        _size = _size & mask;
    }

    // Gap: Expression with old and new
    void scale_and_add(int factor, int offset) {
        _size = _size * factor + offset;
    }
};

// Variant 1: Double from 5 = 10
void variant1_double() {
    Vec v;
    v._size = 5;
    v.double_size();  // _size = 10
    v.at9();  // OK: 10 > 9
}

// Variant 2: Halve from 20 = 10
void variant2_halve() {
    Vec v;
    v._size = 20;
    v.halve_size();  // _size = 10
    v.at9();  // OK: 10 > 9
}

// Variant 3: Halve unsafe (rounds down)
void variant3_halve_unsafe() {
    Vec v;
    v._size = 5;
    v.halve_size();  // _size = 2
    v.at9();  // BUG: 2 <= 9
}

// Variant 4: Complex expression
void variant4_complex() {
    Vec v;
    v._size = 0;
    v.complex_expr(3, 2);  // (3+2)*2-1 = 9
    v.at9();  // BUG: 9 == 9, not > 9
}

// Variant 5: Scale and add
void variant5_scale_add() {
    Vec v;
    v._size = 2;
    v.scale_and_add(3, 5);  // 2*3+5 = 11
    v.at9();  // OK: 11 > 9
}

// Variant 6: Modulo can reduce to 0
void variant6_mod_zero() {
    Vec v;
    v._size = 10;
    v.mod_size(5);  // 10 % 5 = 0
    v.front();  // BUG: _size == 0
}

// Variant 7: Alignment mask
void variant7_align() {
    Vec v;
    v._size = 15;
    v.align_down(~3);  // 15 & ~3 = 12
    v.at9();  // OK: 12 > 9
}

int main() {
    variant1_double();
    variant2_halve();
    variant3_halve_unsafe();
    variant4_complex();
    variant5_scale_add();
    variant6_mod_zero();
    variant7_align();
    return 0;
}
