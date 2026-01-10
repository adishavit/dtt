// Example 472: Buffer access with index sum (guards only) — should COMPILE
// Tests that buf[a + b] is safe when guard conditions are sufficient

void test_compound_guard(int a, int b) {
    int arr[20];
    int* buf = arr;

    if (a >= 0 && a < 10 && b >= 0 && b < 10) {
        buf[a + b] = 0;  // OK: a+b ∈ [0,18] < 20
    }
}

void test_nested_guards(int a, int b) {
    int arr[15];
    int* buf = arr;

    if (a >= 0 && a < 10) {
        if (b >= 0 && b < 5) {
            buf[a + b] = 0;  // OK: a+b ∈ [0,14] < 15
        }
    }
}

void test_triple_guard(int a, int b, int c) {
    int arr[30];
    int* buf = arr;

    if (a >= 0 && a < 10) {
        if (b >= 0 && b < 10) {
            if (c >= 0 && c < 10) {
                buf[a + b + c] = 0;  // OK: a+b+c ∈ [0,27] < 30
            }
        }
    }
}

void test_single_var_guard(int k) {
    int arr[10];
    int* buf = arr;

    if (k >= 0 && k < 10) {
        buf[k] = 0;  // OK: k ∈ [0,9] < 10
    }
}

int main() {
    test_compound_guard(5, 5);
    test_nested_guards(5, 3);
    test_triple_guard(3, 3, 3);
    test_single_var_guard(5);
    return 0;
}
