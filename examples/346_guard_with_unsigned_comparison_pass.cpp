// WHAT IS TESTED:
// Guards with unsigned comparisons should establish var-bound proofs.
//
// Expected: PASS

#include <cstddef>

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// 1. Unsigned index with cast to unsigned on RHS
void test_unsigned_cast_rhs(unsigned i) {
    Vec v;
    v._size = 3;
    if (i < static_cast<unsigned>(v._size)) {
        v[static_cast<int>(i)] = 1;
    }
}

// 2. Unsigned index with implicit conversion
void test_unsigned_implicit(unsigned i) {
    Vec v;
    v._size = 3;
    if (i < (unsigned)v._size) {
        v[(int)i] = 1;
    }
}

// 3. Size_t index (common pattern)
void test_size_t_index(size_t i) {
    Vec v;
    v._size = 3;
    if (i < static_cast<size_t>(v._size)) {
        v[static_cast<int>(i)] = 1;
    }
}

// 4. Unsigned loop variable
void test_unsigned_loop() {
    Vec v;
    v._size = 5;
    for (unsigned i = 0; i < static_cast<unsigned>(v._size); i++) {
        v[static_cast<int>(i)] = i;
    }
}

// 5. Unsigned with while loop
void test_unsigned_while() {
    Vec v;
    v._size = 3;
    unsigned i = 0;
    while (i < static_cast<unsigned>(v._size)) {
        v[static_cast<int>(i)] = i;
        i++;
    }
}

int main() {
    test_unsigned_cast_rhs(1);
    test_unsigned_implicit(1);
    test_size_t_index(1);
    test_unsigned_loop();
    test_unsigned_while();
    return 0;
}
