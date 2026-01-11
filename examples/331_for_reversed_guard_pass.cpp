// WHAT IS TESTED:
// For-loop with reversed comparison (v._size > i instead of i < v._size).
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Reversed comparison: v._size > i
void test_reversed_for() {
    Vec v;
    v._size = 3;

    for (int i = 0; v._size > i; ++i) {
        v[i] = 1;
    }
}

// Reversed with >=
void test_reversed_ge() {
    Vec v;
    v._size = 5;

    for (int i = 0; v._size >= i; ++i) {
        // v[i] would need <= constraint, skip this variant
    }
}

// Mixed: normal and reversed in same function
void test_mixed() {
    Vec v;
    v._size = 4;

    for (int i = 0; i < v._size; ++i) {
        v[i] = 1;
    }

    for (int j = 0; v._size > j; ++j) {
        v[j] = 2;
    }
}

int main() {
    test_reversed_for();
    test_mixed();
    return 0;
}
