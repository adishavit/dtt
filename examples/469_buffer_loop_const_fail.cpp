// Example 469: Loop with constant bound OOB — should FAIL
// Tests that i+1 causing OOB is caught even with constant bounds

void test_const_bound_oob() {
    int arr[10];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        buf[i + 1] = i;  // FAIL: when i=9, i+1=10 is OOB  dtt-error {{out of bounds|cannot verify}}
    }
}

void test_lte_bound_oob() {
    int arr[10];
    int* buf = arr;

    for (int i = 0; i <= 10; i++) {
        buf[i] = i;  // FAIL: when i=10, OOB  dtt-error {{out of bounds|cannot verify}}
    }
}

int main() {
    test_const_bound_oob();
    test_lte_bound_oob();
    return 0;
}
