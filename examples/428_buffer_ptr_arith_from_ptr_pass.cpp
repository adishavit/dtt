// Example 428: Buffer pointer arithmetic from pointer — should PASS
// Tests valid pointer arithmetic from tracked pointer

void test_ptr_arith_valid() {
    int arr[10];
    int* p = arr;          // _base=10, _offset=0
    int* q = p + 5;        // _base=10, _offset=5, _size=5

    int* r = q + 4;        // OK: _offset=9, still within [0,10]
    int* s = q - 3;        // OK: _offset=2, still within [0,10]

    r[0] = 1;              // OK: access at offset 9
    s[0] = 1;              // OK: access at offset 2
}

int main() {
    test_ptr_arith_valid();
    return 0;
}
