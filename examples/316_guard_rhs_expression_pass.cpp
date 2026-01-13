// Tests: Guard with RHS arithmetic expression should establish proof
// Issue #9: FIXED - i < v._size - N now implies i < _size for positive N

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Variant 1: Basic subtraction (i < _size - 1)
void variant1_minus_one(int i) {
    Vec v;
    v._size = 4;
    if (i < v._size - 1) {
        v[i] = 1;  // OK: i < _size - 1 implies i < _size
    }
}

// Variant 2: Larger constant (i < _size - 5)
void variant2_minus_five(int i) {
    Vec v;
    v._size = 10;
    if (i < v._size - 5) {
        v[i] = 1;  // OK: i < _size - 5 implies i < _size
    }
}

// Variant 3: In for-loop condition
void variant3_for_loop() {
    Vec v;
    v._size = 10;
    for (int i = 0; i < v._size - 1; ++i) {
        v[i] = i;  // OK: loop guard implies i < _size
    }
}

// Variant 4: In while-loop condition
void variant4_while_loop() {
    Vec v;
    v._size = 5;
    int i = 0;
    while (i < v._size - 2) {
        v[i] = i;  // OK: while guard implies i < _size
        ++i;
    }
}

int main() {
    variant1_minus_one(0);
    variant2_minus_five(0);
    variant3_for_loop();
    variant4_while_loop();
    return 0;
}
