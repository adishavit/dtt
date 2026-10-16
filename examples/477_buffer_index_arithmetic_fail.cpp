// Example 477: Buffer access with arithmetic expressions OOB — should FAIL
// Tests various index arithmetic patterns that exceed bounds

void test_double_index_oob() {
    int arr[18];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        buf[2 * i] = 0;      // FAIL: 2*i can reach 18, but size is 18 (needs < 19)  dtt-error {{out of bounds|cannot verify}}
    }
}

void test_offset_oob() {
    int arr[12];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        buf[i + 5] = 0;  // FAIL: i+5 can reach 14, but size is 12  dtt-error {{out of bounds|cannot verify}}
    }
}

void test_scaled_sum_oob(int k) {
    int arr[25];
    int* buf = arr;

    if (k >= 0 && k < 10) {
        for (int i = 0; i < 10; i++) {
            buf[2 * i + k] = 0;  // FAIL: 2*i+k can reach 27, but size is 25  dtt-error {{out of bounds|cannot verify}}
        }
    }
}

void test_triple_sum_oob(int a, int b) {
    int arr[20];
    int* buf = arr;

    if (a >= 0 && a < 10 && b >= 0 && b < 10) {
        for (int i = 0; i < 5; i++) {
            buf[a + b + i] = 0;  // FAIL: a+b+i can reach 22, but size is 20  dtt-error {{out of bounds|cannot verify}}
        }
    }
}

int main() {
    test_double_index_oob();
    test_offset_oob();
    test_scaled_sum_oob(5);
    test_triple_sum_oob(5, 5);
    return 0;
}
