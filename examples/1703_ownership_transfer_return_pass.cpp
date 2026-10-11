// 1703: Ownership transfer via return - DTT correctly handles these
// Functions that open/allocate a resource and return it transfer ownership
// to the caller. DTT recognizes return of tracked resource as ownership
// transfer and does not flag a leak inside the wrapper.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Variant 1: fopen wrapper returns FILE* (giflib pattern)
static FILE* open_log(const char* path, const char* mode) {
    FILE* f = fopen(path, mode);
    if (!f) return NULL;
    return f;  // Ownership transferred to caller
}

void variant1_fopen_wrapper() {
    FILE* f = open_log("/dev/null", "w");
    if (f) fclose(f);
}

// Variant 3: malloc wrapper that initializes before returning
struct Buffer {
    size_t len;
    char data[];
};

static Buffer* buffer_create(size_t n) {
    Buffer* b = (Buffer*)malloc(sizeof(Buffer) + n);
    if (!b) return NULL;
    b->len = n;
    memset(b->data, 0, n);
    return b;  // Ownership transferred to caller
}

void variant3_malloc_init_wrapper() {
    Buffer* b = buffer_create(64);
    if (b) free(b);
}

// Variant 5: tmpfile wrapper with setup
static FILE* create_temp() {
    FILE* f = tmpfile();
    if (!f) return NULL;
    fprintf(f, "header\n");
    return f;  // Ownership transferred to caller
}

void variant5_tmpfile_wrapper() {
    FILE* f = create_temp();
    if (f) fclose(f);
}

int main() {
    variant1_fopen_wrapper();
    variant3_malloc_init_wrapper();
    variant5_tmpfile_wrapper();
    return 0;
}
