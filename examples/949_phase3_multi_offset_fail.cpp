// 949_phase3_multi_offset_fail.cpp
// Multiple offset parameters (p = buf + a + b): the call site checks the
// callee's whole bound expression, not only the first offset parameter.

#include <cstddef>
#include <cstring>

// Pattern: cumulative offsets from multiple parameters
void inner_two_offsets(char* buf, size_t a, size_t b) {
    char* p = buf + a;
    p = p + b;  // Second offset contribution
    memset(p, 0, 1);
    // Infers: buf._size_bytes >= 1 + a + b
}

// Call site should warn when total offset exceeds buffer
void outer_two_offsets_bad() {
    char arr[10];
    inner_two_offsets(arr, 4, 8);  // 10 < 1 + 4 + 8 = 13  dtt-error {{call\-site precondition}}
}

int main() { return 0; }
