// 935_phase3_callsite_satisfied_pass.cpp
// Tests: Phase 3 - Call-site checking where offset precondition IS satisfied
//
// Inner functions have offset-based preconditions, and outer functions
// provide buffers large enough to satisfy base._size_bytes >= n + off

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Inner function with offset - infers: base._size_bytes >= n + off
void inner_offset(char* base, size_t off, size_t n) {
    char* p = base + off;
    memcpy(p, "data", n);
}

// Variant 1: Buffer large enough for n + off
void outer_satisfied(size_t n, size_t off) {
    char* buf = (char*)malloc(n + off + 10);  // Extra space
    if (!buf) return;
    inner_offset(buf, off, n);  // OK: buf._size_bytes >= n + off
    free(buf);
}

// Variant 2: Constant offset, large buffer
void outer_const_offset() {
    char buf[200];
    inner_offset(buf, 10, 50);  // OK: 200 >= 50 + 10 = 60
}

// Variant 3: Guard proves it
void outer_guarded(size_t buf_size, size_t off, size_t n) {
    if (n + off > buf_size) return;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    inner_offset(buf, off, n);  // OK: Guarded
    free(buf);
}

int main() { return 0; }
