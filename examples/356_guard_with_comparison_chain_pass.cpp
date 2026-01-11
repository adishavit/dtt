// WHAT IS TESTED:
// Chained comparisons should be recognized (0 <= i && i < _size).
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// 1. Lower bound first: 0 <= i && i < _size
void test_lower_first(int i) {
    Vec v;
    v._size = 3;
    if (0 <= i && i < v._size) {
        v[i] = 1;
    }
}

// 2. Upper bound first: i < _size && i >= 0
void test_upper_first(int i) {
    Vec v;
    v._size = 3;
    if (i < v._size && i >= 0) {
        v[i] = 1;
    }
}

// 3. Combined with other conditions
void test_combined(int i, bool flag) {
    Vec v;
    v._size = 3;
    if (flag && i >= 0 && i < v._size) {
        v[i] = 1;
    }
}

// 4. Different lower bound comparisons
void test_different_lower(int i) {
    Vec v;
    v._size = 3;
    if (i > -1 && i < v._size) {
        v[i] = 1;
    }
}

// 5. Using with multiple accesses
void test_multiple_accesses(int i) {
    Vec v;
    v._size = 5;
    if (0 <= i && i < v._size) {
        v[i] = 1;
        v[i] = 2;
        v[i] = 3;
    }
}

// 6. Nested chain guards
void test_nested_chains(int i, int j) {
    Vec v;
    v._size = 3;
    if (0 <= i && i < v._size) {
        if (0 <= j && j < v._size) {
            v[i] = v[j];
        }
    }
}

int main() {
    test_lower_first(1);
    test_upper_first(1);
    test_combined(1, true);
    test_different_lower(1);
    test_multiple_accesses(2);
    test_nested_chains(1, 2);
    return 0;
}
