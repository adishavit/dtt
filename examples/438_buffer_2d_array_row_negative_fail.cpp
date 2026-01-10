// Example 438: 2D array row negative index — should FAIL
// Tests that negative indices on row pointers are caught

void test_2d_negative() {
    int arr[5][10];
    int* p = arr[2];       // _size=10, _offset=0

    p[-1] = 1;             // FAIL: -1 < 0 (before start)
}

int main() {
    test_2d_negative();
    return 0;
}
