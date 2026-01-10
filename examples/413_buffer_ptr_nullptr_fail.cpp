// Example 413: Buffer pointer set to nullptr — should FAIL
// Tests that nullptr assignment clears buffer tracking and deref fails

void test_nullptr_clears_tracking() {
    int arr[10];
    int* p = arr;          // p: buffer(_size=10)

    p = nullptr;           // p: null (buffer tracking cleared)

    *p = 1;                // ERROR: null pointer dereference
}

int main() {
    test_nullptr_clears_tracking();
    return 0;
}
