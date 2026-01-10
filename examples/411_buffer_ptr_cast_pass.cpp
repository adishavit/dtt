// Example 411: Buffer pointer type casting — should COMPILE
// Tests that casting preserves bounds (converted to new element size)

void test_int_to_char() {
    int arr[10];           // 40 bytes
    int* p = arr + 3;      // _size=7 elements, _size_bytes=28

    char* c = (char*)p;    // _size=28 chars

    c[0] = 1;              // OK: 0 < 28
    c[27] = 2;             // OK: 27 < 28
}

void test_char_to_int() {
    char buf[40];          // 40 bytes
    int* p = (int*)buf;    // _size = 40/4 = 10 ints

    p[0] = 1;              // OK: 0 < 10
    p[9] = 2;              // OK: 9 < 10
}

int main() {
    test_int_to_char();
    test_char_to_int();
    return 0;
}
