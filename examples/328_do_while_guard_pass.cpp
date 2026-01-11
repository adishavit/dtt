// WHAT IS TESTED:
// do-while with known-safe initial values should pass.
// Unlike while-loops, do-while executes body before condition check.
// First iteration relies on constant evaluation, not guard.
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// First iteration: i=0 (constant eval proves safety)
void test_basic_do_while() {
    Vec v;
    v._size = 3;
    int i = 0;

    do {
        v[i] = 1;
        ++i;
    } while (i < v._size);
}

// Multiple do-while loops with known starts
void test_multiple_do_while() {
    Vec v;
    v._size = 5;

    int i = 0;
    do {
        v[i] = 1;
        ++i;
    } while (i < v._size);

    int j = 0;
    do {
        v[j] = 2;
        ++j;
    } while (j < v._size);
}

// Nested do-while with known starts
void test_nested_do_while() {
    Vec v;
    v._size = 3;

    int i = 0;
    do {
        int j = 0;
        do {
            v[j] = i + j;
            ++j;
        } while (j < v._size);
        ++i;
    } while (i < v._size);
}

int main() {
    test_basic_do_while();
    test_multiple_do_while();
    test_nested_do_while();
    return 0;
}
