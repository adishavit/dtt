// Example 426: Buffer pointer negative index — should PASS
// Tests that interior pointers allow valid negative indices

#include <cstddef>

void test_negative_valid() {
    int arr[10];
    int* p = arr + 5;     // _offset=5, _size=5

    // Valid accesses: p[-5] to p[4] maps to arr[0] to arr[9]
    p[-5] = 1;            // OK: arr[0]
    p[-1] = 1;            // OK: arr[4]
    p[0] = 1;             // OK: arr[5]
    p[4] = 1;             // OK: arr[9]
}

int main() {
    test_negative_valid();
    return 0;
}
