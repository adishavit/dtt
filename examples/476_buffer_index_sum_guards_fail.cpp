// Example 476: Buffer access with index sum (guards only) OOB — should FAIL
// Tests that buf[a + b] OOB is caught when combined guard bounds exceed size

void test_compound_guard_oob(int a, int b) {
    int arr[15];
    int* buf = arr;

    if (a >= 0 && a < 10 && b >= 0 && b < 10) {
        buf[a + b] = 0;  // FAIL: a+b can reach 18, but size is 15  dtt-error {{out of bounds}}
    }
}

void test_nested_guards_oob(int a, int b) {
    int arr[12];
    int* buf = arr;

    if (a >= 0 && a < 10) {
        if (b >= 0 && b < 5) {
            buf[a + b] = 0;  // FAIL: a+b can reach 13, but size is 12  dtt-error {{out of bounds}}
        }
    }
}

void test_triple_guard_oob(int a, int b, int c) {
    int arr[20];
    int* buf = arr;

    if (a >= 0 && a < 10) {
        if (b >= 0 && b < 10) {
            if (c >= 0 && c < 10) {
                buf[a + b + c] = 0;  // FAIL: a+b+c can reach 27, but size is 20  dtt-error {{out of bounds}}
            }
        }
    }
}

int main() {
    test_compound_guard_oob(5, 5);
    test_nested_guards_oob(5, 3);
    test_triple_guard_oob(3, 3, 3);
    return 0;
}
