// Example 459: Loop with reversed comparison — should COMPILE
// Tests that n > i is treated same as i < n

void test_reversed_loop() {
    int arr[10];
    int* buf = arr;  // _size = 10

    for (int i = 0; 10 > i; i++) {  // 10 > i instead of i < 10
        buf[i] = 0;  // Should be OK: same semantics
    }
}

int main() {
    test_reversed_loop();
    return 0;
}
