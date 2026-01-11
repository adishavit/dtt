// WHAT IS TESTED:
// Constexpr bound violation should fail.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

constexpr int kSize = 3;

void test_out_of_bounds() {
    Vec v;
    v._size = kSize;
    v[10] = 1;  // ERROR: 10 >= 3
}

int main() { test_out_of_bounds(); return 0; }
