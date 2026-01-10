// Example 473: Buffer access with arithmetic expressions — should COMPILE
// Tests various index arithmetic patterns

void test_double_index() {
    int arr[20];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        buf[2 * i] = 0;      // OK: 2*i ∈ [0,18] < 20
        buf[2 * i + 1] = 1;  // OK: 2*i+1 ∈ [1,19] < 20
    }
}

void test_index_with_offset() {
    int arr[15];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        buf[i + 3] = 0;  // OK: i+3 ∈ [3,12] < 15
    }
}

void test_subtraction_with_guard(int k) {
    int arr[10];
    int* buf = arr;

    if (k >= 5 && k < 15) {
        buf[k - 5] = 0;  // OK: k-5 ∈ [0,9] < 10
    }
}

void test_scaled_sum(int k) {
    int arr[50];
    int* buf = arr;

    if (k >= 0 && k < 10) {
        for (int i = 0; i < 10; i++) {
            buf[2 * i + k] = 0;  // OK: 2*i+k ∈ [0,27] < 50
        }
    }
}

void test_complex_expression(int a, int b) {
    int arr[100];
    int* buf = arr;

    if (a >= 0 && a < 10 && b >= 0 && b < 10) {
        for (int i = 0; i < 5; i++) {
            buf[a + b + i] = 0;  // OK: a+b+i ∈ [0,22] < 100
        }
    }
}

int main() {
    test_double_index();
    test_index_with_offset();
    test_subtraction_with_guard(10);
    test_scaled_sum(5);
    test_complex_expression(5, 5);
    return 0;
}
