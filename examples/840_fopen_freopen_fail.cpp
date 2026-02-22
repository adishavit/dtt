// 840: freopen — unchecked return causes null dereference
// freopen can return NULL. After fp = freopen(...), the old non-null
// proof from if(fp) must be invalidated.

#include <cstdio>

// Variant 1: freopen return not checked (original stream closed, new may fail)
void variant1_freopen_unchecked() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        fp = freopen("test2.txt", "r", fp);
        fgetc(fp);  // FAIL: fp may be null after freopen
    }
}

// Variant 2: freopen(NULL, ...) for mode change — still can fail
void variant2_freopen_mode_change() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        fp = freopen(NULL, "w", fp);
        fgetc(fp);  // FAIL: fp may be null even for mode change
    }
}

// Variant 3: freopen result assigned to different variable
void variant3_freopen_different_var() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        FILE* fp2 = freopen("test2.txt", "r", fp);
        fgetc(fp2);  // FAIL: fp2 may be null
    }
}

int main() { return 0; }
