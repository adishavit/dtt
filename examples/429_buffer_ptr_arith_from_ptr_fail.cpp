// Example 429: Buffer pointer arithmetic from pointer OOB — should FAIL
// Tests that pointer arithmetic from tracked pointer is bounds-checked

void test_ptr_arith_oob() {
    int arr[10];
    int* p = arr;          // _base=10, _offset=0
    int* q = p + 5;        // _base=10, _offset=5

    int* r = q + 6;        // ERROR: offset 11 > 10
}

int main() {
    test_ptr_arith_oob();
    return 0;
}
