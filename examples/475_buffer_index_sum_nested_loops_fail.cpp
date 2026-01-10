// Example 475: Buffer access with index sum (nested loops) OOB — should FAIL
// Tests that buf[i + j] OOB is caught when combined bounds exceed buffer size

void test_nested_oob() {
    int arr[15];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            buf[i + j] = 0;  // FAIL: i+j can reach 18, but size is 15
        }
    }
}

void test_triple_nested_oob() {
    int arr[25];
    int* buf = arr;

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            for (int k = 0; k < 10; k++) {
                buf[i + j + k] = 0;  // FAIL: i+j+k can reach 27, but size is 25
            }
        }
    }
}

void test_asymmetric_oob() {
    int arr[20];
    int* buf = arr;

    for (int i = 0; i < 15; i++) {
        for (int j = 0; j < 10; j++) {
            buf[i + j] = 0;  // FAIL: i+j can reach 23, but size is 20
        }
    }
}

int main() {
    test_nested_oob();
    test_triple_nested_oob();
    test_asymmetric_oob();
    return 0;
}
