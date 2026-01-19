// 933_phase3_pointer_copy_pass.cpp
// Tests: Phase 3 - Pointer copy with offset propagation
//
// When q = p and p = base + off, the base origin should propagate to q

#include <cstddef>
#include <cstring>

// Variant 1: Simple copy
void inner_copy(char* base, size_t off, size_t n) {
    char* p = base + off;
    char* q = p;  // q inherits base origin from p
    memcpy(q, "data", n);  // Should infer: base._size_bytes >= n + off
}

// Variant 2: Copy then modify
void inner_copy_modify(char* base, size_t off, size_t extra, size_t n) {
    char* p = base + off;
    char* q = p;
    q += extra;  // Accumulate more offset
    memcpy(q, "data", n);  // Should infer: base._size_bytes >= n + off + extra
}

// Variant 3: Multiple copies
void inner_multi_copy(char* base, size_t off, size_t n) {
    char* p = base + off;
    char* q = p;
    char* r = q;
    memcpy(r, "data", n);  // Should infer: base._size_bytes >= n + off
}

int main() { return 0; }
