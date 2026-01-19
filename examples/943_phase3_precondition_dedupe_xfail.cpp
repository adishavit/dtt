// 943_phase3_precondition_dedupe_xfail.cpp
// Gap: inferred preconditions are de-duplicated by path+kind, so multiple
// size constraints on the same buffer are collapsed.

#include <cstddef>
#include <cstring>

void inner_multi(char* buf, size_t small, size_t big) {
    memset(buf, 0, small);
    memset(buf, 0, big);  // Stronger constraint should also be enforced.
}

void inner_multi_expr(char* buf, size_t n) {
    memset(buf, 0, n);
    memset(buf, 0, n + 8);  // Stronger constraint should also be enforced.
}

// Variant 1: buffer satisfies small but not big.
void outer_small_ok_big_bad() {
    char buf[8];
    inner_multi(buf, 4, 12);  // Should warn: 8 < 12
}

// Variant 2: expression-based stronger bound dropped.
void outer_expr_big_bad() {
    char buf[16];
    inner_multi_expr(buf, 12);  // Should warn: 16 < 12 + 8
}

int main() { return 0; }
