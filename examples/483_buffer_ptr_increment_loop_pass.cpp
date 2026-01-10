// Example 483: Pointer increment in loop — should COMPILE
// Tests that valid pointer increment patterns in loops are allowed

void test_loop_within_bounds() {
    int arr[10];
    int* p = arr;
    for (int i = 0; i < 10; i++) {
        *p = i;  // OK: offset = i, and i < 10 = size
        p++;
    }
}

void test_loop_partial() {
    int arr[10];
    int* p = arr;
    for (int i = 0; i < 5; i++) {
        *p = i;  // OK: offset = i, and i < 5 < 10
        p++;
    }
}

void test_preincrement_loop() {
    int arr[10];
    int* p = arr;
    for (int i = 0; i < 9; i++) {
        ++p;     // offset becomes i+1
        *p = i;  // OK: offset = i+1, max is 9 < 10
    }
}

int main() {
    test_loop_within_bounds();
    test_loop_partial();
    test_preincrement_loop();
    return 0;
}
