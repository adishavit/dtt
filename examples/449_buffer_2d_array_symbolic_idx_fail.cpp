// Example 449: 2D array with symbolic row index OOB — should FAIL
// Tests that OOB is caught even with runtime row index

int get_row();

void test_symbolic_row_oob() {
    int arr[5][10];
    int row = get_row();

    int* p = arr[row];     // _size=10
    p[10] = 1;             // FAIL: 10 >= 10 (one past end)  dtt-error {{out of bounds}}
}

int main() {
    test_symbolic_row_oob();
    return 0;
}
