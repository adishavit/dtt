// Example 414: Buffer pointer restore after nullptr — should COMPILE
// Tests that reassigning to array restores buffer tracking

void test_restore_tracking() {
    int arr[10];
    int* p = arr;          // p: buffer(_size=10)

    p = nullptr;           // p: null

    p = arr;               // p: buffer(_size=10) restored

    p[5] = 1;              // OK: 5 < 10
}

int main() {
    test_restore_tracking();
    return 0;
}
