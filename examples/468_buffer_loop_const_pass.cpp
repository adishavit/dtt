// Example 468: Loop with constant bound — should COMPILE
// Tests that valid accesses in constant-bound loops are allowed

void test_const_bound_valid() {
    int arr[10];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        buf[i] = i;  // OK: i ranges 0-9, all valid
    }
}

void test_const_bound_valid_with_offset() {
    int arr[10];
    int* buf = arr;

    for (int i = 0; i < 9; i++) {
        buf[i + 1] = i;  // OK: i+1 ranges 1-9, all valid
    }
}

int main() {
    test_const_bound_valid();
    test_const_bound_valid_with_offset();
    return 0;
}
