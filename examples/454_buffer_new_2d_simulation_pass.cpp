// Example 454: Simulated 2D array via struct — should COMPILE
// Tests 2D-like access pattern using struct with row arrays

struct Matrix3x4 {
    int row0[4];
    int row1[4];
    int row2[4];
};

void test_matrix_struct() {
    Matrix3x4* m = new Matrix3x4;

    int* r0 = m->row0;  // _size=4
    int* r1 = m->row1;  // _size=4
    int* r2 = m->row2;  // _size=4

    r0[3] = 1;  // OK: last of row0
    r1[0] = 2;  // OK: first of row1
    r2[3] = 3;  // OK: last of row2

    delete m;
}

// Alternative: array of rows
struct Row { int data[8]; };

void test_row_array() {
    Row* rows = new Row[5];  // 5 rows of 8 ints each

    int* r = rows[2].data;   // _size=8
    r[7] = 42;               // OK: last element

    delete[] rows;
}

int main() {
    test_matrix_struct();
    test_row_array();
    return 0;
}
