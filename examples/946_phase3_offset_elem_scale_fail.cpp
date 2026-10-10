// 946_phase3_offset_elem_scale_xfail.cpp
// Gap: Offset parameters are element counts but call-site checking doesn't scale by element size.
// When inner has `int* p = buf + off`, off is in elements (not bytes).
// At call site, actualOffset should be multiplied by sizeof(int) = 4.

#include <cstddef>
#include <cstring>

void inner_int_offset(int* buf, size_t off) {
    int* p = buf + off;  // off is in elements (each 4 bytes)
    memset(p, 0, sizeof(int));  // writes 4 bytes starting at buf + off*4 bytes
}

// Variant 1: Buffer too small when offset properly scaled
// off=2 elements = 8 bytes offset, plus 4 bytes written = 12 bytes needed
// buf is only 8 bytes - should warn!
void outer_int_offset_bad() {
    int arr[2];  // 8 bytes total
    inner_int_offset(arr, 2);  // Should warn: 8 < 4 + (2 * 4) = 12  dtt-error {{call\-site precondition 'buf\._size_bytes >= 4\+\(\(off\*4\)\)'}}
}

// Variant 2: Same pattern with char* base showing byte-level offset works
void inner_char_offset(char* buf, size_t off) {
    char* p = buf + off;  // off is in bytes (sizeof(char) = 1)
    memset(p, 0, 1);
}

void outer_char_offset_bad() {
    char arr[4];
    inner_char_offset(arr, 4);  // Should warn: 4 < 1 + 4 = 5  dtt-error {{call\-site precondition 'buf\._size_bytes >= 1\+\(off\)'}}
}

int main() { return 0; }
