// Example 423: Buffer pointer void* cast — should PASS
// Tests that casting to void* preserves _size_bytes

#include <cstddef>

void test_void_cast() {
    int arr[10];           // 40 bytes
    void* v = arr;         // _size_bytes = 40
    char* c = (char*)v;    // _size = 40 chars

    c[39] = 1;             // OK: 39 < 40
}

int main() {
    test_void_cast();
    return 0;
}
