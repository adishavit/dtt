// WHAT IS TESTED:
// Array element access out of bounds should fail.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test_out_of_bounds() {
    Vec arr[2];
    arr[0]._size = 3;
    arr[0][10] = 1;  // ERROR: 10 >= 3  dtt-error {{out of bounds}}
}

int main() { test_out_of_bounds(); return 0; }
