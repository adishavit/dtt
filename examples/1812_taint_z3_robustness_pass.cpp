// RUN: %clang++ -fplugin=%plugin -std=c++20 -fsyntax-only %s
// EXPECTED: No errors - all tainted values are properly bounded by guards
// Tests Z3 expression conversion robustness for complex guard patterns.
// All variants use the early-return pattern (if (...) return) which is
// the pattern supported by checkTaintGuardWithZ3.

#include <cstdlib>
#include <cstring>

// Variant 1: sizeof in guard (already supported, regression test)
void variant_sizeof_guard() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    char buf[256];
    if (len > sizeof(buf)) return;
    memset(buf, 0, len);  // OK: bounded by sizeof
}

// Variant 2: ternary in guard expression
void variant_ternary_guard() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    if (len > ((1 > 0) ? 128 : 64)) return;
    char buf[256];
    memset(buf, 0, len);  // OK: bounded by ternary (128)
}

// Variant 3: constant arithmetic in bound expression
void variant_arithmetic_guard() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    if (len > 256 - 16) return;  // 256 - 16 = 240
    char buf[256];
    memset(buf, 0, len);  // OK: len <= 240
}

// Variant 4: boolean && with bound check
void variant_bool_and_guard() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    char buf[128];
    if (len > sizeof(buf) || !env) return;
    memset(buf, 0, len);  // OK: bounded
}

// Variant 5: double negation
void variant_negation_guard() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    char buf[128];
    if (!(len <= sizeof(buf))) return;
    memset(buf, 0, len);  // OK: !(len <= 128) means len > 128, negated = len <= 128
}

// Variant 6: subtraction in bound
void variant_subtraction_guard() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    if (len > 256 - 16) return;
    char buf[256];
    memset(buf, 0, len);  // OK: len <= 240
}

// Variant 7: multiplication in bound
void variant_multiplication_guard() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    if (len > 32 * 4) return;
    char buf[256];
    memset(buf, 0, len);  // OK: len <= 128
}
