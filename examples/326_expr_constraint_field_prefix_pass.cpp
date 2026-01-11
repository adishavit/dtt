// WHAT IS TESTED:
// Expression constraints on fields should substitute field values.
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=_size + 1 > 0")]]
    void check_plus() const {}

    [[clang::annotate("dtt:requires=_size * 2 >= 0")]]
    void check_times() const {}

    [[clang::annotate("dtt:requires=_size - 1 < _size")]]
    void check_minus() const {}
};

// Basic field expression
void test_basic() {
    Vec v;
    v._size = 0;
    v.check_plus();  // 0 + 1 > 0 = true
}

// Multiple field expressions
void test_multiple() {
    Vec v;
    v._size = 5;
    v.check_plus();   // 5 + 1 > 0 = true
    v.check_times();  // 5 * 2 >= 0 = true
    v.check_minus();  // 5 - 1 < 5 = true
}

// With zero size
void test_zero_size() {
    Vec v;
    v._size = 0;
    v.check_plus();   // 0 + 1 > 0 = true
    v.check_times();  // 0 * 2 >= 0 = true
}

int main() {
    test_basic();
    test_multiple();
    test_zero_size();
    return 0;
}
