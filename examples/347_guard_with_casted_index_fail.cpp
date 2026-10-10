// WHAT IS TESTED:
// Casted index out of bounds should fail.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Index out of bounds
void test_out_of_bounds() {
    Vec v;
    v._size = 3;
    v[10] = 1;  // ERROR: 10 >= 3  dtt-error {{out of bounds}}
}

int main() { test_out_of_bounds(); return 0; }
