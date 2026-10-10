// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 873_memcpy_array_bounds_fail.cpp
// Tests: memcpy over-read from local arrays
// DTT SHOULD catch these buffer over-reads

#include <cstring>
#include <cstdint>

void test_memcpy_overread() {
    char src[10];
    char dst[100];
    memcpy(dst, src, 100);  // ERROR: reading 100 bytes from 10-byte buffer  // dtt-error {{CWE-126}}
}

void test_memcpy_overread_small() {
    uint8_t buf[8];
    uint8_t out[16];
    memcpy(out, buf, 16);  // ERROR: reading 16 bytes from 8-byte buffer  // dtt-error {{CWE-126}}
}

void test_memmove_overread() {
    int arr[5];
    int out[10];
    memmove(out, arr, 10 * sizeof(int));  // ERROR: reading 10 ints from 5-int array  // dtt-error {{CWE-126}}
}

int main() { return 0; }
