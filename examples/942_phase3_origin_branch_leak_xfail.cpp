// 942_phase3_origin_branch_leak_xfail.cpp
// Gap: PointerBaseOrigins is not saved/restored across branches/loops, so
// base origins can leak from a taken path into the join.

#include <cstddef>
#include <cstring>

void inner_branch(char* base, char* other, size_t off, size_t n, bool flag) {
    char* p = other;
    if (flag) {
        p = base + off;
    }
    memset(p, 0, n);
}

void inner_loop(char* base, char* other, size_t off, size_t n, int iters) {
    char* p = other;
    for (int i = 0; i < iters; ++i) {
        p = base + off;
    }
    memset(p, 0, n);
}

// Variant 1: branch may keep p = other, but origin leaks from then-branch.
void outer_branch_other_small() {
    char base[64];
    char other[8];
    inner_branch(base, other, 10, 12, false);  // Should warn: other < n
}

// Variant 2: loop may not execute, but origin still leaks.
void outer_loop_other_small() {
    char base[64];
    char other[8];
    inner_loop(base, other, 10, 12, 0);  // Should warn: other < n
}

int main() { return 0; }
