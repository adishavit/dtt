// RUN: %clang++ -fplugin=%plugin -std=c++20 -fsyntax-only %s
// EXPECTED: No errors - taint bounded by for-loop condition
// Tests: Z3 taint guard works inside for-loop body

#include <cstdlib>
#include <cstring>

// Variant 1: for with variable bound on tainted limit
void variant_for_variable() {
    char *env = getenv("LIMIT");
    if (!env) return;
    unsigned int limit = (unsigned int)atoi(env);
    unsigned int cap = 128;
    char buf[256];
    for (unsigned int i = 0; limit <= cap; i++) {
        memset(buf, 0, limit);  // OK: limit <= cap (128)
        break;
    }
}

// Variant 2: for with arithmetic bound
void variant_for_arithmetic() {
    char *env = getenv("SZ");
    if (!env) return;
    unsigned int sz = (unsigned int)atoi(env);
    char buf[256];
    for (unsigned int i = 0; sz + 8 <= 256; i++) {
        memset(buf, 0, sz);  // OK: sz + 8 <= 256 → sz <= 248
        break;
    }
}
