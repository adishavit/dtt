// RUN: %clang++ -fplugin=%plugin -std=c++20 -fsyntax-only %s
// EXPECTED: No errors - taint bounded by Z3 in compound conditions
// Tests: Z3 taint supplement in extractAllGuards path
//
// When extractAllGuards partially parses a compound condition (e.g.,
// ptr != NULL is extracted but len <= max_len is not due to variable RHS),
// Z3 supplements the missing taint guard.

#include <cstdlib>
#include <cstring>

// Variant 1: ptr guard extracted, len guard missed (variable RHS)
void variant_partial_compound() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    unsigned int max_len = 128;
    char buf[256];
    char *ptr = buf;
    if (ptr != nullptr && len <= max_len) {
        memset(buf, 0, len);  // OK: Z3 proves len bounded
    }
}

// Variant 2: null guard + arithmetic bound
void variant_null_plus_arithmetic() {
    char *env = getenv("SZ");
    if (!env) return;
    unsigned int sz = (unsigned int)atoi(env);
    char buf[256];
    char *dst = buf;
    if (dst != nullptr && sz + 16 <= 256) {
        memset(dst, 0, sz);  // OK: Z3 proves sz + 16 <= 256
    }
}

// Variant 3: else-branch of compound condition
void variant_compound_else() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    unsigned int max_len = 128;
    char buf[256];
    if (len > max_len) {
        // error path
    } else {
        memset(buf, 0, len);  // OK: Z3 proves !(len > max_len)
    }
}
