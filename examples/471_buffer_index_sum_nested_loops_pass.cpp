// Example 471: Buffer access with index sum (nested loops) — should COMPILE
// Tests that buf[i + j] is safe when both loop bounds are sufficient

void test_nested_loops_sum() {
    int arr[20];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            buf[i + j] = 0;  // OK: i+j ∈ [0,18] < 20
        }
    }
}

void test_nested_loops_tight() {
    int arr[10];
    int* buf = arr;

    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            buf[i + j] = 0;  // OK: i+j ∈ [0,8] < 10
        }
    }
}

void test_triple_nested() {
    int arr[30];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            for (int k = 0; k < 10; k++) {
                buf[i + j + k] = 0;  // OK: i+j+k ∈ [0,27] < 30
            }
        }
    }
}

void test_different_bounds() {
    int arr[25];
    int* buf = arr;

    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 5; j++) {
            buf[i + j] = 0;  // OK: i+j ∈ [0,24] < 25
        }
    }
}

int main() {
    test_nested_loops_sum();
    test_nested_loops_tight();
    test_triple_nested();
    test_different_bounds();
    return 0;
}
