// 840: freopen properly transfers resource ownership
// Tests that freopen's arg2.@open = CLOSED annotation satisfies
// fopen's scope_exit before acquiring a new resource.

#include <cstdio>

// Variant 1: freopen with null checks
void test_freopen_guard() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        fp = freopen("test2.txt", "r", fp);
        if (fp) {
            char buf[100];
            fgets(buf, sizeof(buf), fp);  // OK: double guarded
            fclose(fp);
        }
    }
}

// Variant 2: freopen for mode change (NULL filename)
void test_freopen_mode_change() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        fp = freopen(nullptr, "w", fp);
        if (fp) {
            fputs("hello", fp);
            fclose(fp);
        }
    }
}

int main() { return 0; }
