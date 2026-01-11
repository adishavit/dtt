// WHAT IS TESTED:
// Guards with casted indices should be recognized.
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// 1. Long to int cast
void test_long_cast(long i) {
    Vec v;
    v._size = 3;
    if (static_cast<int>(i) < v._size) {
        v[static_cast<int>(i)] = 1;
    }
}

// 2. Short to int cast
void test_short_cast(short i) {
    Vec v;
    v._size = 3;
    if (static_cast<int>(i) < v._size) {
        v[static_cast<int>(i)] = 1;
    }
}

// 3. C-style cast
void test_c_style_cast(long i) {
    Vec v;
    v._size = 3;
    if ((int)i < v._size) {
        v[(int)i] = 1;
    }
}

// 4. Char to int
void test_char_cast(char c) {
    Vec v;
    v._size = 128;
    if (static_cast<int>(c) < v._size && static_cast<int>(c) >= 0) {
        v[static_cast<int>(c)] = 1;
    }
}

// 5. Double to int (truncation)
void test_double_cast(double d) {
    Vec v;
    v._size = 5;
    if (static_cast<int>(d) < v._size && static_cast<int>(d) >= 0) {
        v[static_cast<int>(d)] = 1;
    }
}

int main() {
    test_long_cast(1);
    test_short_cast(1);
    test_c_style_cast(1);
    test_char_cast('A');
    test_double_cast(2.5);
    return 0;
}
