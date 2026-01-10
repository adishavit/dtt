// Example 463: Increment index causing OOB — should FAIL
// Tests that OOB is caught for both pre and post increment

void test_post_increment_oob() {
    int arr[3];
    int* p = arr;
    int i = 3;

    p[i++] = 1;  // FAIL: i=3 is OOB (post-increment uses current value)
}

void test_pre_increment_oob() {
    int arr[3];
    int* p = arr;
    int i = 2;

    p[++i] = 1;  // FAIL: ++i = 3, OOB (pre-increment uses incremented value)
}

int main() {
    test_post_increment_oob();
    test_pre_increment_oob();
    return 0;
}
