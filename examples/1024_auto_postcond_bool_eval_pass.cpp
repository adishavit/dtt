// WHAT IS TESTED:
// Bool argument evaluation for ternary postconditions.
// Verifies that bool args like 'true' are properly evaluated to 1/0.
//
// EXPECTED: DTT correctly evaluates bool arguments in ternary expressions.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    void set_bool(bool b) {
        _size = b ? 10 : 5;
    }
};

void test_bool_true() {
    Vec v;
    v._size = 0;
    v.set_bool(true);  // _size = 10 (true ? 10 : 5)
    v.front();         // OK: 10 > 0
}

void test_bool_false() {
    Vec v;
    v._size = 0;
    v.set_bool(false); // _size = 5 (false ? 10 : 5)
    v.front();         // OK: 5 > 0
}

int main() {
    test_bool_true();
    test_bool_false();
    return 0;
}
