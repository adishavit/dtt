// WHAT IS TESTED:
// Guards with constexpr bounds in if-statements should be recognized.
// Note: For-loop bounds with constexpr (i < kSize) is a known gap - use (i < v._size) instead.
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

constexpr int kSize = 3;
constexpr int kLargeSize = 100;
constexpr int kHalf = kLargeSize / 2;
static const int kStaticSize = 5;

// 1. If guard with constexpr bound
void test_if_constexpr_guard(int i) {
    Vec v;
    v._size = kSize;
    if (i < kSize) {
        v[i] = 1;
    }
}

// 2. For loop with direct _size reference (workaround for constexpr gap)
void test_for_loop_direct() {
    Vec v;
    v._size = kSize;
    for (int i = 0; i < v._size; i++) {
        v[i] = i;
    }
}

// 3. Larger constexpr in if guard
void test_large_constexpr(int i) {
    Vec v;
    v._size = kLargeSize;
    if (i < kLargeSize) {
        v[i] = 1;
    }
}

// 4. Constexpr expression in if guard
void test_constexpr_expr(int i) {
    Vec v;
    v._size = kHalf;
    if (i < kHalf) {
        v[i] = 1;
    }
}

// 5. Static const in if guard
void test_static_const(int i) {
    Vec v;
    v._size = kStaticSize;
    if (i < kStaticSize) {
        v[i] = 1;
    }
}

// 6. Nested if with constexpr
void test_nested_if(int i) {
    Vec v;
    v._size = kSize;
    if (i >= 0) {
        if (i < kSize) {
            v[i] = 1;
        }
    }
}

int main() {
    test_if_constexpr_guard(1);
    test_for_loop_direct();
    test_large_constexpr(50);
    test_constexpr_expr(25);
    test_static_const(2);
    test_nested_if(1);
    return 0;
}
