// Example 462: Post/pre-increment in array index — should COMPILE
// Tests that increment operators in index expressions are tracked correctly

void test_post_increment() {
    int arr[5];
    int* p = arr;
    int i = 0;

    // Post-increment: i++ evaluates to current value, then increments
    p[i++] = 1;  // Access p[0], i becomes 1
    p[i++] = 2;  // Access p[1], i becomes 2
    p[i++] = 3;  // Access p[2], i becomes 3
    p[i++] = 4;  // Access p[3], i becomes 4
    p[i++] = 5;  // Access p[4], i becomes 5 - all valid
}

void test_pre_increment() {
    int arr[5];
    int* p = arr;
    int i = 0;

    // Pre-increment: ++i increments first, then evaluates
    p[++i] = 1;  // i becomes 1, access p[1]
    p[++i] = 2;  // i becomes 2, access p[2]
    p[++i] = 3;  // i becomes 3, access p[3]
    p[++i] = 4;  // i becomes 4, access p[4] - all valid
}

int main() {
    test_post_increment();
    test_pre_increment();
    return 0;
}
