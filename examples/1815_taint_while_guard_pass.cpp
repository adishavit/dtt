// RUN: %clang++ -fplugin=%plugin -std=c++20 -fsyntax-only %s
// EXPECTED: No errors - taint bounded by while-loop condition
// Tests: Z3 taint guard works inside while-loop body

#include <cstdlib>
#include <cstring>

// Variant 1: while with variable bound
void variant_while_variable() {
    char *env = getenv("COUNT");
    if (!env) return;
    unsigned int count = (unsigned int)atoi(env);
    unsigned int limit = 100;
    char buf[256];
    while (count <= limit) {
        memset(buf, 0, count);  // OK: count <= limit (100)
        break;
    }
}

// Variant 2: while with arithmetic
void variant_while_arithmetic() {
    char *env = getenv("N");
    if (!env) return;
    unsigned int n = (unsigned int)atoi(env);
    char buf[256];
    while (n + 10 <= 256) {
        memset(buf, 0, n);  // OK: n + 10 <= 256 → n <= 246
        break;
    }
}
