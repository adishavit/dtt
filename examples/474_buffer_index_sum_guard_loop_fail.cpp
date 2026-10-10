// Example 474: Buffer access with index sum (guard + loop) OOB — should FAIL
// Tests that buf[i + k] OOB is caught when bounds exceed buffer size

void test_guard_loop_oob(int k) {
    int arr[15];
    int* buf = arr;

    if (k >= 0 && k < 10) {
        for (int i = 0; i < 10; i++) {
            buf[i + k] = 0;  // FAIL: i+k can reach 18, but size is 15  dtt-error {{out of bounds}}
        }
    }
}

void test_tight_bound_oob(int k) {
    int arr[18];
    int* buf = arr;

    if (k >= 0 && k < 10) {
        for (int i = 0; i < 10; i++) {
            buf[i + k] = 0;  // FAIL: i+k can reach 18, but size is 18 (needs < 19)  dtt-error {{out of bounds}}
        }
    }
}

int main() {
    test_guard_loop_oob(5);
    test_tight_bound_oob(5);
    return 0;
}
