// Example 430: 2D array row pointer — should FAIL
// Tests that row pointers from 2D arrays track inner dimension size
//
// arr[i] on T arr[M][N] gives a pointer to N elements (the row size)

void test_2d_array_row() {
    int arr[5][10];        // 5 rows of 10 ints
    int* p = arr[2];       // Points to row 2, _size=10

    p[10] = 1;             // FAIL: 10 >= 10 (out of bounds)  dtt-error {{out of bounds}}
}

int main() {
    test_2d_array_row();
    return 0;
}
