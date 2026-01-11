// WHAT IS TESTED:
// Guards on array element fields should work with operator[].
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// 1. Array element guard with operator[]
void test_array_element_operator(int i) {
    Vec arr[2];
    arr[0]._size = 3;
    if (i < arr[0]._size) {
        arr[0][i] = 1;
    }
}

// 2. Multiple array elements
void test_multiple_elements(int i, int j) {
    Vec arr[3];
    arr[0]._size = 3;
    arr[1]._size = 5;
    if (i < arr[0]._size) {
        arr[0][i] = 1;
    }
    if (j < arr[1]._size) {
        arr[1][j] = 2;
    }
}

// 3. 2D array
void test_2d_array(int i) {
    Vec arr[2][2];
    arr[0][1]._size = 4;
    if (i < arr[0][1]._size) {
        arr[0][1][i] = 1;
    }
}

// 4. Constant index array element
void test_const_index() {
    Vec arr[2];
    arr[0]._size = 5;
    arr[0][2] = 1;  // 2 < 5
}

int main() {
    test_array_element_operator(1);
    test_multiple_elements(1, 2);
    test_2d_array(2);
    test_const_index();
    return 0;
}
