// 872_memcpy_array_bounds_pass.cpp
// Tests: memcpy with local arrays - safe cases
// DTT should NOT report errors when copy size <= array size

#include <cstring>
#include <cstdint>

void test_memcpy_exact_size() {
    char src[100];
    char dst[100];
    memcpy(dst, src, 100);  // OK: exactly fits
}

void test_memcpy_smaller_size() {
    char src[100];
    char dst[50];
    memcpy(dst, src, 50);  // OK: only copying 50 bytes from 100-byte buffer
}

void test_memcpy_with_sizeof() {
    uint8_t buf[64];
    uint8_t out[64];
    memcpy(out, buf, sizeof(buf));  // OK: sizeof gives exact size
}

void test_memmove_safe() {
    int arr[10];
    memmove(arr + 2, arr, 8 * sizeof(int));  // OK: moving 8 ints within 10-int array
}

int main() { return 0; }
