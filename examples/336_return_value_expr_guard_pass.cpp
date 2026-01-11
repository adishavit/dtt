// WHAT IS TESTED:
// Guards on return value expressions should work.
//
// Expected: PASS

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:returns=_size")]]
    int size() const { return _size; }

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// 1. size() - 1 >= 0 implies size() > 0
void test_minus_expr() {
    Vec v;
    v._size = 1;
    if (v.size() - 1 >= 0) {
        v.front();
    }
}

// 2. size() > 0 directly
void test_direct() {
    Vec v;
    v._size = 5;
    if (v.size() > 0) {
        v.front();
    }
}

// 3. size() >= 1 implies > 0
void test_ge_one() {
    Vec v;
    v._size = 2;
    if (v.size() >= 1) {
        v.front();
    }
}

// 4. Known size value
void test_known() {
    Vec v;
    v._size = 10;
    v.front();  // _size = 10 > 0
}

int main() {
    test_minus_expr();
    test_direct();
    test_ge_one();
    test_known();
    return 0;
}
