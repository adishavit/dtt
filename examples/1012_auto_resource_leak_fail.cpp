// 1012: Resource leak detection for unchecked pointer results
// With `when` guards, ghost fields are only set when _nonnull is proven.
// These unchecked results have unknown _nonnull, so @open/@allocated are
// never set — leaks go undetected. Requires deferred guard evaluation
// (applying guarded postconditions when entering branches that prove the guard).

#include <cstdio>
#include <cstdlib>

// Variant 1: FILE* leak - fopen without fclose
void variant1_file_leak() {
    FILE* f = fopen("data.txt", "r");  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: f.@open == 1, needs fclose before scope exit
}

// Variant 2: Memory leak - malloc without free
void variant2_malloc_leak() {
    void* p = malloc(100);  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: p.@allocated == 1, needs free before scope exit
}

// Variant 3: Multiple leaks
void variant3_multiple_leaks() {
    FILE* f = fopen("data.txt", "r");  // dtt-error {{resource pointer that is never null-checked}}
    void* p = malloc(100);  // dtt-error {{resource pointer that is never null-checked}}
    void* q = calloc(10, sizeof(int));  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: three leaks
}

int main() {
    return 0;
}
