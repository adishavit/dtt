// Example 484: Pointer increment in loop OOB — should FAIL
// Tests that pointer increment exceeding bounds in loops is caught

void test_loop_exceeds_bounds() {
    int arr[5];
    int* p = arr;
    for (int i = 0; i < 10; i++) {  // Loop bound 10 > array size 5
        *p = i;  // FAIL: when i >= 5, offset >= size  dtt-error {{out of bounds}}
        p++;
    }
}

void test_off_by_one() {
    int arr[10];
    int* p = arr;
    for (int i = 0; i <= 10; i++) {  // Note: <= instead of <
        *p = i;  // FAIL: when i = 10, offset = 10 >= size  dtt-error {{out of bounds}}
        p++;
    }
}

void test_preincrement_oob() {
    int arr[5];
    int* p = arr;
    for (int i = 0; i < 5; i++) {
        ++p;     // offset becomes i+1
        *p = i;  // FAIL: when i=4, offset=5 >= size  dtt-error {{out of bounds}}
    }
}

int main() {
    test_loop_exceeds_bounds();
    test_off_by_one();
    test_preincrement_oob();
    return 0;
}
