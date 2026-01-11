// WHAT IS TESTED:
// Unsigned access without guard should fail.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Int index out of bounds (cast tests don't expose violations well)
void test_out_of_bounds() {
    Vec v;
    v._size = 3;
    v[10] = 1;  // ERROR: 10 >= 3
}

int main() { test_out_of_bounds(); return 0; }
