// 944_phase2_bound_expr_arg_missing_xfail.cpp
// Gap: getParameterIndex doesn't decompose expressions like n + 10, so
// call-site checks drop the bound entirely.

#include <cstddef>
#include <cstring>

void inner_expr_const(char* buf, size_t n) {
    memset(buf, 0, n + 10);
}

void inner_expr_param(char* buf, size_t n, size_t extra) {
    memset(buf, 0, n + extra);
}

// Variant 1: constant tail should be enforced.
void outer_expr_const_bad() {
    char buf[12];
    inner_expr_const(buf, 8);  // Should warn: 12 < 8 + 10
}

// Variant 2: parameter tail should be enforced.
void outer_expr_param_bad() {
    char buf[16];
    inner_expr_param(buf, 8, 12);  // Should warn: 16 < 8 + 12
}

int main() { return 0; }
