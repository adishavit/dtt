// 941_phase3_callsite_offset_ignored_xfail.cpp
// Gap: call-site buffer size ignores pointer offset, so remaining capacity
// is overestimated for arguments like buf + off.

#include <cstddef>
#include <cstring>

void inner_simple(char* buf, size_t n) {
    memset(buf, 0, n);
}

// Variant 1: offset leaves only 2 bytes, but n = 4.
void outer_offset_known() {
    char arr[8];
    char* p = arr + 6;
    inner_simple(p, 4);  // Should warn: remaining 2 < 4
}

// Variant 2: larger buffer, still insufficient remaining space.
void outer_offset_known2() {
    char arr[16];
    char* p = arr + 10;
    inner_simple(p, 8);  // Should warn: remaining 6 < 8
}

int main() { return 0; }
