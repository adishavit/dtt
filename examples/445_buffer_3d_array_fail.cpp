// Example 445: 3D array row out of bounds — should FAIL
// Tests that OOB access on 3D array innermost row is caught

void test_3d_oob() {
    int cube[3][4][5];
    int* row = cube[1][2]; // _size=5

    row[5] = 1;            // FAIL: 5 >= 5 (one past end)  dtt-error {{out of bounds}}
}

int main() {
    test_3d_oob();
    return 0;
}
