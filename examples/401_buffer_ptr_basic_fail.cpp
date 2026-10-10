// Example 401: Buffer pointer OOB access — should FAIL
// Tests that out-of-bounds access through decayed pointer is caught

void test_oob() {
    int arr[10];
    int* p = arr;       // p: _size = 10

    p[10] = 1;          // ERROR: 10 >= 10  dtt-error {{out of bounds}}
}

int main() {
    test_oob();
    return 0;
}
