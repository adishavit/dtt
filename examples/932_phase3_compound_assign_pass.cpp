// 932_phase3_compound_assign_pass.cpp
// Tests: Phase 3 - Compound assignment (p += off)
//
// When p starts as base and is modified via p += off,
// DTT should track: base._size_bytes >= n + off

#include <cstddef>
#include <cstring>

// Variant 1: Simple compound assignment
void inner_compound(char* base, size_t off, size_t n) {
    char* p = base;
    p += off;
    memcpy(p, "data", n);  // Should infer: base._size_bytes >= n + off
}

// Variant 2: Multiple compound assignments
void inner_compound_multi(char* base, size_t a, size_t b, size_t n) {
    char* p = base;
    p += a;
    p += b;
    memcpy(p, "data", n);  // Should infer: base._size_bytes >= n + a + b
}

// Variant 3: Compound subtract
void inner_compound_sub(char* base, size_t off, size_t back, size_t n) {
    char* p = base;
    p += off;
    p -= back;
    memcpy(p, "data", n);  // Should infer: base._size_bytes >= n + (off - back)
}

// Variant 4: Reassignment p = p + n
void inner_reassign(char* base, size_t off, size_t n) {
    char* p = base;
    p = p + off;
    memcpy(p, "data", n);  // Should infer: base._size_bytes >= n + off
}

int main() { return 0; }
