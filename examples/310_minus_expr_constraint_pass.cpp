// WHAT IS TESTED:
// Expression constraints with subtraction should be parsed as arithmetic.
// "arg0 - 1 < _size" should correctly evaluate arg0 - 1, not just arg0.
//
// Expected: PASS (all constraints satisfied)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 - 1 < _size")]]
    int& at_minus_one(int idx) { return *reinterpret_cast<int*>(0); }

    [[clang::annotate("dtt:requires=arg0 + 1 < _size")]]
    int& at_plus_one(int idx) { return *reinterpret_cast<int*>(0); }

    [[clang::annotate("dtt:requires=arg0 * 2 < _size")]]
    int& at_times_two(int idx) { return *reinterpret_cast<int*>(0); }
};

void test_minus() {
    Vec v;
    v._size = 3;
    v.at_minus_one(3);  // 3 - 1 = 2 < 3, should pass
}

void test_plus() {
    Vec v;
    v._size = 5;
    v.at_plus_one(2);  // 2 + 1 = 3 < 5, should pass
}

void test_times() {
    Vec v;
    v._size = 10;
    v.at_times_two(4);  // 4 * 2 = 8 < 10, should pass
}

int main() {
    test_minus();
    test_plus();
    test_times();
    return 0;
}
