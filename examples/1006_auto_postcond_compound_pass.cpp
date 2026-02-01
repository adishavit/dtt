// WHAT IS TESTED:
// Auto-postcondition inference for compound assignment operators.
// Patterns: _size += n, _size -= n (both constant and parameter values)
//
// EXPECTED: DTT infers postconditions from compound assignments on 'this' fields
// and applies them at call sites to allow safe access.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 4")]]
    int& at4() { return data[4]; }

    // NO annotation - DTT should infer ensures=_size == old(_size) + 3
    void addThree() { _size += 3; }

    // NO annotation - DTT should infer ensures=_size == old(_size) - 2
    void removeTw() { _size -= 2; }

    // NO annotation - DTT should infer ensures=_size == old(_size) + arg0
    void addN(int n) { _size += n; }

    // NO annotation - DTT should infer ensures=_size == old(_size) - arg0
    void removeN(int n) { _size -= n; }
};

// Variant 1: Constant compound addition
void variant1_constant_add() {
    Vec v;
    v._size = 0;
    v.addThree();  // _size = 0 + 3 = 3
    v.front();     // OK: _size == 3 > 0
}

// Variant 2: Constant compound subtraction
void variant2_constant_sub() {
    Vec v;
    v._size = 7;
    v.removeTw();  // _size = 7 - 2 = 5
    v.at4();       // OK: _size == 5 > 4
}

// Variant 3: Parameter compound addition
void variant3_param_add() {
    Vec v;
    v._size = 2;
    v.addN(3);     // _size = 2 + 3 = 5
    v.at4();       // OK: _size == 5 > 4
}

// Variant 4: Mixed operations
void variant4_mixed() {
    Vec v;
    v._size = 3;
    v.addThree();  // _size = 6
    v.removeN(1);  // _size = 5
    v.at4();       // OK: _size == 5 > 4
}

int main() {
    variant1_constant_add();
    variant2_constant_sub();
    variant3_param_add();
    variant4_mixed();
    return 0;
}
