// 938_phase3_stale_origin_reassign_xfail.cpp
// Gap: PointerBaseOrigins is not cleared on reassignment, so stale base
// origin rewrites preconditions to the wrong parameter.

#include <cstddef>
#include <cstring>

void inner_reassign(char* base, char* other, size_t off, size_t n) {
    char* p = base + off;
    p = other;  // Should clear base origin.
    memset(p, 0, n);
}

void inner_reassign_then_bump(char* base, char* other, size_t off, size_t n) {
    char* p = base + off;
    p = other;  // Should clear base origin.
    p += 1;
    memset(p, 0, n);
}

// Variant 1: other is too small, base is large.
void outer_other_too_small() {
    char base[64];
    char other[8];
    inner_reassign(base, other, 10, 12);  // Should warn: other < n
}

// Variant 2: same pattern with extra pointer arithmetic.
void outer_other_too_small_bump() {
    char base[64];
    char other[8];
    inner_reassign_then_bump(base, other, 10, 12);  // Should warn: other < n
}

int main() { return 0; }
