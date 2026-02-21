// 1718: Deferred guard evaluation - no false positives on correct code
// Null-checked results with proper cleanup should pass cleanly.

#include <cstdio>
#include <cstdlib>

// Variant 1: if (f) { fclose(f); } — proper cleanup inside branch
void variant1_file_if_close() {
    FILE* f = fopen("data.txt", "r");
    if (f) {
        int c = fgetc(f);
        (void)c;
        fclose(f);
    }
}

// Variant 2: if (!f) return; ... fclose(f); — early return with cleanup
void variant2_file_early_return_close() {
    FILE* f = fopen("data.txt", "r");
    if (!f) return;
    int c = fgetc(f);
    (void)c;
    fclose(f);
}

// Variant 3: malloc with if (p) { free(p); }
void variant3_malloc_if_free() {
    void* p = malloc(100);
    if (p) {
        free(p);
    }
}

// Variant 4: malloc early return with free
void variant4_malloc_early_return_free() {
    void* p = malloc(100);
    if (!p) return;
    free(p);
}

// Variant 5: unchecked result — should NOT trigger (no false positive)
// The when guard is never satisfied, so no ghost field is set.
void variant5_unchecked_fopen() {
    FILE* f = fopen("data.txt", "r");
    (void)f;
    // No null check → no deferred eval → no @open set → no error
}

// Variant 6: unchecked malloc — same as above
void variant6_unchecked_malloc() {
    void* p = malloc(100);
    (void)p;
}

int main() {
    return 0;
}
