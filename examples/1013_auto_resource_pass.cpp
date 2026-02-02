// Example 1013: Auto resource tracking - proper cleanup should pass
// Tests that DTT accepts properly managed resources

#include <cstdio>
#include <cstdlib>

// Variant 1: FILE* properly closed
void variant1_file_ok() {
    FILE* f = fopen("data.txt", "r");
    if (f) {
        fclose(f);  // OK: closes the file
    }
}

// Variant 2: Memory properly freed
void variant2_malloc_ok() {
    void* p = malloc(100);
    if (p) {
        free(p);  // OK: frees the memory
    }
}

// Variant 3: Multiple resources properly managed
void variant3_multiple_ok() {
    FILE* f = fopen("data.txt", "r");
    void* p = malloc(100);
    void* q = calloc(10, sizeof(int));

    if (f) fclose(f);
    if (p) free(p);
    if (q) free(q);
}

// Variant 4: Early return after null check
void* variant4_early_return() {
    void* p = malloc(100);
    if (!p) return nullptr;  // OK: null check, no leak
    free(p);
    return p;  // Actually returns freed pointer, but that's a different issue
}

int main() {
    return 0;
}
