// Example 448: 2D array with symbolic row index — should COMPILE
// Tests that row extraction works even with runtime row index

int get_row();

void test_symbolic_row_index() {
    int arr[5][10];        // 5 rows of 10 ints
    int row = get_row();   // Runtime row index

    int* p = arr[row];     // _size=10 (regardless of which row)
    p[9] = 1;              // OK: last element of any row
}

void test_loop_row_access() {
    double matrix[3][8];

    for (int i = 0; i < 3; i++) {
        double* row = matrix[i];  // _size=8
        row[7] = 1.0;             // OK: last element
    }
}

int main() {
    test_symbolic_row_index();
    test_loop_row_access();
    return 0;
}
