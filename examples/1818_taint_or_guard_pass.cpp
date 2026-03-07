// RUN: %clang++ -fplugin=%plugin -std=c++20 -fsyntax-only %s
// EXPECTED: No errors - taint bounded by Z3 in OR guard patterns
// Tests: Z3 taint supplement in extractOrGuards path

#include <cstdlib>
#include <cstring>

// Variant 1: OR early-return with variable bound
void variant_or_early_return() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    unsigned int max_len = 128;
    char buf[256];
    if (len > max_len || !env) return;
    memset(buf, 0, len);  // OK: !(len > max_len) holds after return
}

// Variant 2: OR else-branch with variable bound
void variant_or_else() {
    char *env = getenv("LEN");
    if (!env) return;
    unsigned int len = (unsigned int)atoi(env);
    unsigned int max_len = 128;
    char buf[256];
    if (len > max_len || len == 0) {
        // error/skip path
    } else {
        memset(buf, 0, len);  // OK: !(len > max_len) && !(len == 0)
    }
}
