// Example 464: Post/pre-decrement in array index — should COMPILE
// Tests that decrement operators in index expressions are tracked correctly

void test_post_decrement() {
    int arr[5];
    int* p = arr;
    int i = 4;

    // Post-decrement: i-- evaluates to current value, then decrements
    p[i--] = 1;  // Access p[4], i becomes 3
    p[i--] = 2;  // Access p[3], i becomes 2
    p[i--] = 3;  // Access p[2], i becomes 1
    p[i--] = 4;  // Access p[1], i becomes 0
    p[i--] = 5;  // Access p[0], i becomes -1 - all valid accesses
}

void test_pre_decrement() {
    int arr[5];
    int* p = arr;
    int i = 5;

    // Pre-decrement: --i decrements first, then evaluates
    p[--i] = 1;  // i becomes 4, access p[4]
    p[--i] = 2;  // i becomes 3, access p[3]
    p[--i] = 3;  // i becomes 2, access p[2]
    p[--i] = 4;  // i becomes 1, access p[1]
    p[--i] = 5;  // i becomes 0, access p[0] - all valid
}

int main() {
    test_post_decrement();
    test_pre_decrement();
    return 0;
}
