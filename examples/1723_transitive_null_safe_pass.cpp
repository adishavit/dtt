// 1723_transitive_null_safe_pass.cpp
// Tests: Safe code with null checks should NOT trigger transitive null warnings
//
// When a caller properly checks for null before passing to a wrapper that
// transitively requires _nonnull, no diagnostic should be emitted.

#include <cstdio>

// Inner function that calls fgetc (requires _nonnull via libc.dtt)
static void read_from(FILE* fp) {
    fgetc(fp);
}

// Wrapper
static void process(FILE* fp) {
    read_from(fp);
}

// Variant 1: Null-checked fopen result is safe
static void variant1_checked_fopen() {
    FILE* fp = fopen("/tmp/test.txt", "r");
    if (fp) {
        process(fp);  // Safe: fp is non-null after check
        fclose(fp);
    }
}

// Variant 2: Known non-null literal (stdin/stdout)
// Note: stdin may not be tracked as non-null by DTT, so skip this variant

int main() {
    variant1_checked_fopen();
    return 0;
}
