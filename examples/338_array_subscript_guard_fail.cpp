// WHAT IS TESTED:
// Array subscript guards should catch violations.
//
// Expected: ERROR

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& at(int idx) { return *reinterpret_cast<int*>(0); }
};

// Constant out of bounds
void test_out_of_bounds() {
    Vec arr[2];
    arr[0]._size = 3;
    arr[0].at(10);  // ERROR: 10 >= 3
}

int main() { test_out_of_bounds(); return 0; }
