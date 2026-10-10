// 1718: Deferred guard evaluation - leak detection through null checks
// When fopen/malloc results are null-checked, DTT should retroactively
// apply guarded postconditions and detect resource leaks.
// Tests the pattern: acquire → null-check → forget release.

#include <cstdio>
#include <cstdlib>

// Variant 1: if (f) { forgot fclose }
void variant1_file_if_no_close() {
    FILE* f = fopen("data.txt", "r");  // dtt-error {{scope-exit constraint}}
    if (f) {
        int c = fgetc(f);
        (void)c;
        // ERROR: f.@open == 1, forgot fclose
    }
}

// Variant 2: if (!f) return; forgot fclose
void variant2_file_early_return_no_close() {
    FILE* f = fopen("data.txt", "r");  // dtt-error {{scope-exit constraint}}
    if (!f) return;
    int c = fgetc(f);
    (void)c;
    // ERROR: f.@open == 1, forgot fclose
}

// Variant 3: malloc with if (p) { forgot free }
void variant3_malloc_if_no_free() {
    void* p = malloc(100);  // dtt-error {{scope-exit constraint}}
    if (p) {
        // ERROR: p.@allocated == 1, forgot free
    }
}

// Variant 4: malloc with early return, forgot free
void variant4_malloc_early_return_no_free() {
    void* p = malloc(100);  // dtt-error {{scope-exit constraint}}
    if (!p) return;
    // ERROR: p.@allocated == 1, forgot free
}

int main() {
    return 0;
}
