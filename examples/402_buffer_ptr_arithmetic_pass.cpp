// Example 402: Buffer pointer arithmetic — should COMPILE
// Tests that pointer arithmetic correctly adjusts _offset and _size

void test_ptr_plus() {
    int arr[10];
    int* p = arr;       // p: _base_length=10, _offset=0, _size=10

    int* q = p + 3;     // q: _base_length=10, _offset=3, _size=7

    q[0] = 1;           // OK: 0 < 7
    q[6] = 2;           // OK: 6 < 7
}

void test_address_of_middle() {
    int arr[10];
    int* p = &arr[5];   // p: _base_length=10, _offset=5, _size=5

    p[0] = 1;           // OK: 0 < 5
    p[4] = 2;           // OK: 4 < 5
}

void test_chain_arithmetic() {
    int arr[10];
    int* p = arr + 2;   // _offset=2, _size=8
    int* q = p + 3;     // _offset=5, _size=5

    q[4] = 1;           // OK: 4 < 5
}

void test_ptr_minus() {
    int arr[10];
    int* p = arr + 5;   // _offset=5, _size=5
    int* q = p - 2;     // _offset=3, _size=7

    q[6] = 1;           // OK: 6 < 7
}

int main() {
    test_ptr_plus();
    test_address_of_middle();
    test_chain_arithmetic();
    test_ptr_minus();
    return 0;
}
