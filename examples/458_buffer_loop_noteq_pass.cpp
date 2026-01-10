// Example 458: Loop with != (iterator style) — should COMPILE
// Tests that != is treated like < for loop invariants

void test_noteq_loop() {
    int arr[10];
    int* buf = arr;  // _size = 10

    for (int i = 0; i != 10; i++) {
        buf[i] = 0;  // Should be OK: i != 10 implies i < 10
    }
}

int main() {
    test_noteq_loop();
    return 0;
}
