// 937_phase3_offset_arg_missing_xfail.cpp
// Gap: offsetArgIndex not populated for symbolic offsets, so call-site checks
// only enforce "n" instead of "n + off".

#include <cstddef>
#include <cstring>

void inner_offset(char* base, size_t off, size_t n) {
    char* p = base + off;
    memset(p, 0, n);
}

// Variant 1: Constant offset should be enforced at call site.
void outer_const_violation() {
    char buf[8];
    inner_offset(buf, 6, 4);  // 8 < 4 + 6  dtt-error {{call\-site precondition 'base\._size_bytes >= n\+\(off\)'}}
}

// Variant 2: Larger constants.
void outer_const_violation2() {
    char buf[16];
    inner_offset(buf, 12, 10);  // 16 < 10 + 12  dtt-error {{call\-site precondition 'base\._size_bytes >= n\+\(off\)'}}
}

// Variant 3: Symbolic offset should still be part of the bound.
void outer_symbolic_off(size_t off) {
    char buf[8];
    inner_offset(buf, off, 6);  // Should warn unless off <= 2  dtt-error {{call\-site precondition 'base\._size_bytes >= n\+\(off\)'}}
}

int main() { return 0; }
