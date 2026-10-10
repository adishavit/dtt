// Example 465: Decrement index causing OOB — should FAIL
// Tests that OOB is caught for both pre and post decrement

void test_post_decrement_oob() {
    int arr[3];
    int* p = arr;
    int i = -1;

    p[i--] = 1;  // FAIL: i=-1 is negative index (post-decrement uses current value)  dtt-error {{out of bounds}}
}

void test_pre_decrement_oob() {
    int arr[3];
    int* p = arr;
    int i = 0;

    p[--i] = 1;  // FAIL: --i = -1, negative index (pre-decrement uses decremented value)  dtt-error {{out of bounds}}
}

int main() {
    test_post_decrement_oob();
    test_pre_decrement_oob();
    return 0;
}
