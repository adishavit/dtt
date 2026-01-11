// WHAT IS TESTED:
// Guards on array subscript base paths should work.
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& at(int idx) { return *reinterpret_cast<int*>(0); }
};

// Guard on arr[0] with runtime index
void test_runtime_index(int i) {
    Vec arr[2];
    arr[0]._size = 3;

    if (i < arr[0]._size) {
        arr[0].at(i);  // Guarded
    }
}

// Guard on arr[0] with constant index
void test_constant_index() {
    Vec arr[2];
    arr[0]._size = 5;
    arr[0].at(2);  // 2 < 5
}

// Guard on different array elements
void test_different_elements(int i, int j) {
    Vec arr[3];
    arr[0]._size = 2;
    arr[1]._size = 3;
    arr[2]._size = 4;

    if (i < arr[0]._size) {
        arr[0].at(i);
    }
    if (j < arr[1]._size) {
        arr[1].at(j);
    }
}

int main() {
    test_runtime_index(1);
    test_constant_index();
    test_different_elements(1, 2);
    return 0;
}
