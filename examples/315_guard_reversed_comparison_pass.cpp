// WHAT IS TESTED:
// Var-bound guards should recognize both "i < v._size" and "v._size > i".
// The reversed comparison form should also prove the constraint.
//
// Expected: PASS (all guards prove their constraints)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }

    [[clang::annotate("dtt:requires=arg0 <= _size")]]
    int& at(int idx) { return *reinterpret_cast<int*>(0); }
};

void test_reversed_gt(int i) {
    Vec v;
    v._size = 3;

    if (v._size > i) {
        v[i] = 1;  // Safe: v._size > i proves i < _size
    }
}

void test_reversed_ge(int i) {
    Vec v;
    v._size = 3;

    if (v._size >= i) {
        v.at(i);  // Safe: v._size >= i proves i <= _size
    }
}

int main() {
    test_reversed_gt(1);
    test_reversed_ge(1);
    return 0;
}
