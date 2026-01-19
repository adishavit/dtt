// 950_phase3_complex_offset_expr_xfail.cpp
// Known limitation: Complex offset expressions (off+1, off*2) not tracked as param.
// Only simple parameter references are captured for call-site checking.

#include <cstddef>
#include <cstring>

// Pattern: offset with arithmetic expression
void inner_offset_plus_const(char* buf, size_t off) {
    char* p = buf + (off + 4);  // Complex expression: off + 4
    memset(p, 0, 1);
    // Should infer: buf._size_bytes >= 1 + off + 4 = 5 + off
    // Actually gets correct byte offset, but off+4 may not be tracked as param
}

void inner_offset_scaled(char* buf, size_t off) {
    char* p = buf + (off * 2);  // Complex expression: off * 2
    memset(p, 0, 1);
    // Should infer: buf._size_bytes >= 1 + off*2
}

// Call site checks should use the complex expressions
void outer_complex_bad() {
    char arr[8];
    inner_offset_plus_const(arr, 6);  // Should warn: 8 < 5 + 6 = 11
    inner_offset_scaled(arr, 5);      // Should warn: 8 < 1 + 5*2 = 11
}

int main() { return 0; }
