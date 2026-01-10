// Example 444: 3D array row extraction — should COMPILE
// Tests that extracting rows from 3D arrays tracks correctly

void test_3d_to_2d() {
    int cube[3][4][5];     // 3 planes of 4x5 matrices

    // cube[i] gives int[4][5] - a 2D array, not directly a pointer
    // Can't do: int* p = cube[0]; (that's int(*)[5], not int*)
    // But can do: int* p = cube[0][0]; (that's int[5] decaying to int*)

    int* row = cube[1][2]; // _size=5 (innermost dimension)
    row[4] = 1;            // OK: last element
}

void test_3d_innermost() {
    double tensor[2][3][8];
    double* p = tensor[0][1];  // _size=8
    p[7] = 3.14;               // OK: last element
}

int main() {
    test_3d_to_2d();
    test_3d_innermost();
    return 0;
}
