// RUN: %clang++ -fplugin=%plugin -std=c++20 -fsyntax-only %s
// EXPECTED: No errors - taint bounded by else-branch guard pattern
// Tests: Z3 taint guard works in else-branch (negated condition)
//
// if (len > max) { error(); } else { use(len); }
// Inside else: !(len > max) → len <= max → taint bounded.

#include <cstdlib>
#include <cstring>

// Variant 1: simple else-branch bound with variable RHS
void variant_else_variable() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    unsigned int max_len = 128;
    char buf[256];
    if (len > max_len) {
        return;  // error path
    } else {
        memset(buf, 0, len);  // OK: len <= max_len in else
    }
}

// Variant 2: else-branch with arithmetic condition
void variant_else_arithmetic() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    char buf[256];
    if (len + 16 > 256) {
        return;
    } else {
        memset(buf, 0, len);  // OK: len + 16 <= 256 → len <= 240
    }
}

// Variant 3: else-branch where then does NOT terminate
void variant_else_nonterminating_then() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    unsigned int cap = 128;
    char buf[256];
    if (len > cap) {
        len = cap;  // clamp, don't return
    } else {
        memset(buf, 0, len);  // OK: len <= cap in else
    }
}
