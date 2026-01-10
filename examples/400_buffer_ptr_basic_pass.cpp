// Example 400: Buffer pointer basic tracking — should COMPILE
// Tests that pointers derived from arrays inherit bounds and valid access works
//
// This is the foundational test for buffer pointer tracking.
// DTT should track _size from array decay and allow valid indexing.

void test_array_decay() {
    int arr[10];
    int* p = arr;       // p inherits: _size = 10

    p[0] = 1;           // OK: 0 < 10
    p[5] = 2;           // OK: 5 < 10
    p[9] = 3;           // OK: 9 < 10
}

void test_address_of_first() {
    int arr[10];
    int* p = &arr[0];   // p inherits: _size = 10

    p[9] = 1;           // OK: 9 < 10
}

void test_direct_array_access() {
    int arr[10];

    arr[0] = 1;         // OK: 0 < 10
    arr[9] = 2;         // OK: 9 < 10
}

int main() {
    test_array_decay();
    test_address_of_first();
    test_direct_array_access();
    return 0;
}
