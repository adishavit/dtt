// RUN: %clang++ -fplugin=%plugin -std=c++20 -fsyntax-only %s
// EXPECTED: No errors - taint bounded by then-body guard pattern
// Tests: Z3 taint guard for if-then-body with non-constant bounds
//
// extractGuard handles "var OP const" in then-blocks. These variants use
// expressions that extractGuard CANNOT parse (variable RHS, arithmetic LHS)
// but Z3 can prove bounded. Requires checkTaintGuardWithZ3 for then-body.

#include <cstdlib>
#include <cstring>

// Variant 1: variable on RHS — extractGuard needs constant
void variant_variable_rhs() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    unsigned int max_len = 128;
    char buf[256];
    if (len <= max_len) {
        memset(buf, 0, len);  // OK: len bounded by max_len
    }
}

// Variant 2: arithmetic on LHS
void variant_arithmetic_lhs() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    char buf[256];
    if (len + 16 <= 256) {
        memset(buf, 0, len);  // OK: len + 16 <= 256 implies len <= 240
    }
}

// Variant 3: different variable name for bound
void variant_cap_variable() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    unsigned int cap = 256;
    char buf[256];
    if (len <= cap) {
        memset(buf, 0, len);  // OK: len bounded by cap
    }
}
