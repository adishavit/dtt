// WHAT IS TESTED:
// Unified expression parser for auto-postcondition inference.
// Tests that all arithmetic expressions are correctly parsed and tracked.
//
// EXPECTED: DTT uses unified expression parser to handle all cases.
// RESULT: DTT should catch all bugs after parser integration.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }

    // Multiplication: _size = _size * 2
    void double_size() {
        _size = _size * 2;
    }

    // Division: _size = _size / 2
    void halve_size() {
        _size = _size / 2;
    }

    // Complex: _size = (a + b) * 2
    void set_complex(int a, int b) {
        _size = (a + b) * 2;
    }

    // Modulo: _size = _size % n
    void mod_by(int n) {
        _size = _size % n;
    }
};

// Variant 1: Double from 5 = 10
void variant1_double_safe() {
    Vec v;
    v._size = 5;
    v.double_size();  // _size = 10
    v.at9();          // OK: 10 > 9
}

// Variant 2: Halve from 4 = 2 (unsafe)
void variant2_halve_unsafe() {
    Vec v;
    v._size = 4;
    v.halve_size();   // _size = 2
    v.at9();          // BUG: 2 <= 9
}

// Variant 3: Complex expression
void variant3_complex() {
    Vec v;
    v._size = 0;
    v.set_complex(3, 2);  // _size = (3+2)*2 = 10
    v.at9();              // OK: 10 > 9
}

// Variant 4: Modulo can make zero
void variant4_mod_zero() {
    Vec v;
    v._size = 10;
    v.mod_by(5);      // _size = 10 % 5 = 0
    v.front();        // BUG: 0 <= 0
}

int main() {
    variant1_double_safe();
    variant2_halve_unsafe();
    variant3_complex();
    variant4_mod_zero();
    return 0;
}
