// 1732: Unchecked resource warning — fopen/malloc result used without null check
// When a guarded resource function's result is never null-checked before scope
// exit, DTT should warn about the unchecked resource (distinct from leak detection).

#include <cstdio>
#include <cstdlib>

// Variant 1: FILE* fopen result never checked
void variant1_unchecked_fopen() {
    FILE* f = fopen("data.txt", "r");  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: f is a resource pointer from fopen() and is never null-checked
}

// Variant 2: malloc result never checked
void variant2_unchecked_malloc() {
    void* p = malloc(100);  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: p is a resource pointer from malloc() and is never null-checked
}

int main() { return 0; }
