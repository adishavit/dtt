// Example 480: Pointer increment tracking — should COMPILE
// Tests that p++ correctly updates tracked offset

void test_increment_valid() {
    int arr[5];
    int* p = arr;  // offset=0, size=5
    *p = 0;        // OK: offset 0 < 5
    p++;           // offset becomes 1
    *p = 1;        // OK: offset 1 < 5
    p++;           // offset becomes 2
    *p = 2;        // OK: offset 2 < 5
}

void test_preincrement_valid() {
    int arr[5];
    int* p = arr;
    *p = 0;        // OK
    ++p;           // offset becomes 1
    *p = 1;        // OK
}

void test_decrement_valid() {
    int arr[5];
    int* p = arr + 4;  // offset=4, size=1
    *p = 0;            // OK: offset 0 into remaining
    p--;               // Now at arr+3
    *p = 1;            // OK
    --p;               // Now at arr+2
    *p = 2;            // OK
}

void test_increment_to_end() {
    int arr[3];
    int* p = arr;
    *p = 0; p++;   // offset 0 -> 1
    *p = 1; p++;   // offset 1 -> 2
    *p = 2;        // offset 2, OK (last valid)
    // p++ here would make it point past end, but we don't dereference
}

int main() {
    test_increment_valid();
    test_preincrement_valid();
    test_decrement_valid();
    test_increment_to_end();
    return 0;
}
