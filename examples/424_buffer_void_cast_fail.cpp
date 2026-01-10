// Example 424: Buffer pointer void* cast OOB — should FAIL
// Tests that casting through void* catches OOB

#include <cstddef>

void test_void_cast_oob() {
    int arr[10];           // 40 bytes
    void* v = arr;         // _size_bytes = 40
    char* c = (char*)v;    // _size = 40 chars

    c[40] = 1;             // ERROR: 40 >= 40
}

int main() {
    test_void_cast_oob();
    return 0;
}
