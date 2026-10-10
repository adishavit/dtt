// 947_phase3_offset_char_debug_xfail.cpp
// Debug test: char* offset should work (element size = 1, no scaling needed)

#include <cstddef>
#include <cstring>

void inner_char_offset(char* buf, size_t off) {
    char* p = buf + off;  // p derived from buf with offset off
    memset(p, 0, 1);      // Should infer buf._size_bytes >= 1 + off
}

void outer_char_offset_bad() {
    char arr[8];
    inner_char_offset(arr, 10);  // Should warn: 8 < 1 + 10 = 11  dtt-error {{call\-site precondition 'buf\._size_bytes >= 1\+\(off\)'}}
}

int main() { return 0; }
