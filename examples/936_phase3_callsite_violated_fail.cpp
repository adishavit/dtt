// 936_phase3_callsite_violated_fail.cpp
// Tests: Phase 3 - Call-site checking where offset precondition is NOT satisfied
//
// Inner functions have offset-based preconditions, outer functions
// provide buffers that are TOO SMALL when offset is considered.
// Should produce warnings/errors.

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Inner function with offset - infers: base._size_bytes >= n + off
void inner_offset(char* base, size_t off, size_t n) {
    char* p = base + off;
    memcpy(p, "data", n);
}

// Variant 1: Buffer only sized for n, not n + off
void outer_too_small(size_t n, size_t off) {
    char* buf = (char*)malloc(n);  // Only n bytes, but need n + off!
    if (!buf) return;
    // SHOULD WARN: buf._size_bytes (n) < n + off
    inner_offset(buf, off, n);
    free(buf);
}

// Variant 2: Constant case - provably too small
void outer_const_too_small() {
    char buf[50];
    // SHOULD WARN: 50 < 40 + 20 = 60
    inner_offset(buf, 20, 40);
}

// Variant 3: Untracked buffer with offset
void outer_untracked(char* buf, size_t off, size_t n) {
    // SHOULD WARN: cannot prove buf._size_bytes >= n + off
    inner_offset(buf, off, n);
}

int main() { return 0; }
