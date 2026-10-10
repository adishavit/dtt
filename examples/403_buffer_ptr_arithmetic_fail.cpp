// Example 403: Buffer pointer arithmetic OOB — should FAIL
// Tests that OOB access after arithmetic is caught

void test_oob_after_arithmetic() {
    int arr[10];
    int* p = arr + 7;   // _offset=7, _size=3

    p[3] = 1;           // ERROR: 3 >= 3 (only indices 0,1,2 valid)  dtt-error {{out of bounds}}
}

int main() {
    test_oob_after_arithmetic();
    return 0;
}
