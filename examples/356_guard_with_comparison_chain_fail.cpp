// WHAT IS TESTED:
// Index out of bounds should fail even with partial chain.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Out of bounds
void test_out_of_bounds() {
    Vec v;
    v._size = 3;
    v[10] = 1;  // ERROR: 10 >= 3
}

int main() { test_out_of_bounds(); return 0; }
