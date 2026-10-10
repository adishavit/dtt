// Example 425: memcpy with void* requires — should FAIL
// Tests requires=dest._size_bytes >= count on memcpy

#include <cstddef>
#include <cstring>

// Local annotation for memcpy
[[clang::annotate("dtt:requires=dest._size_bytes >= count")]]
[[clang::annotate("dtt:requires=src._size_bytes >= count")]]
void* memcpy(void* dest, const void* src, size_t count);

void test_memcpy_violation() {
    int src[10];           // 40 bytes
    int dest[5];           // 20 bytes

    memcpy(dest, src, 30); // ERROR: dest._size_bytes=20 < 30  dtt-error {{arg0\._size_bytes >= arg2}}
}

int main() {
    test_memcpy_violation();
    return 0;
}
