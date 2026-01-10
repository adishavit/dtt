// Example 437: 2D array row valid access — should COMPILE
// Tests that valid accesses within row bounds are allowed

void test_2d_valid_access() {
    int arr[5][10];        // 5 rows of 10 ints
    int* p = arr[2];       // _size=10

    p[0] = 1;              // OK: first element
    p[9] = 2;              // OK: last element
}

void test_2d_multiple_rows() {
    int arr[3][8];
    int* row0 = arr[0];    // _size=8
    int* row1 = arr[1];    // _size=8
    int* row2 = arr[2];    // _size=8

    row0[7] = 1;           // OK: last of row 0
    row1[0] = 2;           // OK: first of row 1
    row2[4] = 3;           // OK: middle of row 2
}

int main() {
    test_2d_valid_access();
    test_2d_multiple_rows();
    return 0;
}
