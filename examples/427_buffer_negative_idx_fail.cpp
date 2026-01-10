// Example 427: Buffer pointer negative index OOB — should FAIL
// Tests that too-negative indices are caught

#include <cstddef>

void test_negative_oob() {
    int arr[10];
    int* p = arr + 5;     // _offset=5, _size=5

    p[-6] = 1;            // ERROR: arr[-1] — before start
}

int main() {
    test_negative_oob();
    return 0;
}
