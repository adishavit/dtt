// 930_phase3_simple_offset_pass.cpp
// Tests: Phase 3 - Simple pointer offset rewriting
//
// When p = base + off and memcpy(p, ..., n) has unverified bounds,
// DTT should emit: base._size_bytes >= n + off (not p._size_bytes >= n)

#include <cstddef>
#include <cstring>

// Variant 1: Simple offset from parameter
void inner_simple(char* base, size_t off, size_t n) {
    char* p = base + off;
    memcpy(p, "data", n);  // Should infer: base._size_bytes >= n + off
}

// Variant 2: Offset with constant
void inner_const_offset(char* base, size_t n) {
    char* p = base + 10;
    memcpy(p, "data", n);  // Should infer: base._size_bytes >= n + 10
}

// Variant 3: Two buffers, both offset
void inner_two_buffers(char* dst_base, char* src_base, size_t off, size_t n) {
    char* dst = dst_base + off;
    char* src = src_base + off;
    memcpy(dst, src, n);  // Should infer: dst_base._size_bytes >= n + off
                          //               src_base._size_bytes >= n + off
}

int main() { return 0; }
