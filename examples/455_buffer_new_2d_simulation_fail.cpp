// Example 455: Simulated 2D array OOB — should FAIL
// Tests OOB in 2D-like access via struct

struct Row { int data[8]; };

void test_row_oob() {
    Row* rows = new Row[5];

    int* r = rows[2].data;   // _size=8
    r[8] = 42;               // FAIL: 8 >= 8 (one past end)

    delete[] rows;
}

int main() {
    test_row_oob();
    return 0;
}
