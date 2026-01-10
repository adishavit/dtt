// Example 470: Buffer access with index sum (guard + loop) — should COMPILE
// Tests that buf[i + k] is safe when guard and loop bounds are sufficient

void test_guard_then_loop(int k) {
    int arr[20];
    int* buf = arr;

    if (k >= 0 && k < 10) {
        for (int i = 0; i < 10; i++) {
            buf[i + k] = 0;  // OK: i ∈ [0,9], k ∈ [0,9], so i+k ∈ [0,18] < 20
        }
    }
}

void test_loop_then_guard(int k) {
    int arr[20];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        if (k >= 0 && k < 10) {
            buf[i + k] = 0;  // OK: same bounds
        }
    }
}

void test_single_guard_each(int k) {
    int arr[15];
    int* buf = arr;

    if (k >= 0) {
        if (k < 5) {
            for (int i = 0; i < 10; i++) {
                buf[i + k] = 0;  // OK: i+k ∈ [0,14] < 15
            }
        }
    }
}

int main() {
    test_guard_then_loop(5);
    test_loop_then_guard(5);
    test_single_guard_each(3);
    return 0;
}
