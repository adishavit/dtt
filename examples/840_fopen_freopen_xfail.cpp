// 840: freopen — XFAIL: unchecked freopen return
// GAP: freopen annotation doesn't reset result._nonnull, so DTT
// doesn't detect null dereference when freopen return is unchecked.

#include <cstdio>

// Variant 1: freopen return not checked (original stream closed, new may fail)
void test_freopen_unchecked() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        fp = freopen("test2.txt", "r", fp);
        // fp could be NULL if freopen fails, but we use it directly
        fgetc(fp);  // FAIL: fp may be null
    }
}

int main() { return 0; }
