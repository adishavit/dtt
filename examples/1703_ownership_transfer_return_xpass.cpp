// 1703: Ownership transfer via return - DTT false positive
// Functions that open/allocate a resource and return it transfer ownership
// to the caller. DTT may flag a resource leak inside the wrapper because it
// sees the acquire without a matching release in the same function scope.
// Found by scanning libwebp (src/utils/utils.c:210) and giflib (lib/egif_lib.c).

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

// Variant 1: fopen wrapper returns FILE* (giflib pattern)
static FILE* open_log(const char* path, const char* mode) {
    FILE* f = fopen(path, mode);
    if (!f) return NULL;
    // DTT sees fopen() with no fclose() in this function scope
    return f;
}

void variant1_fopen_wrapper() {
    FILE* f = open_log("/dev/null", "w");
    if (f) fclose(f);
}

// Variant 2: open + fdopen wrapper (giflib EGifOpenFileName pattern)
// fd acquired then wrapped — two resources, one returned
static FILE* open_as_stream(const char* path) {
    int fd = open(path, 0);
    if (fd < 0) return NULL;
    FILE* f = fdopen(fd, "r");
    if (!f) {
        close(fd);
        return NULL;
    }
    return f;  // DTT: fd and/or FILE* may appear leaked
}

void variant2_open_fdopen_wrapper() {
    FILE* f = open_as_stream("/dev/null");
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
    return b;  // DTT: malloc with no free in this scope
}

void variant3_malloc_init_wrapper() {
    Buffer* b = buffer_create(64);
    if (b) free(b);
}

// Variant 4: popen wrapper
static FILE* run_command(const char* cmd) {
    FILE* p = popen(cmd, "r");
    return p;  // DTT: popen with no pclose
}

void variant4_popen_wrapper() {
    FILE* p = run_command("echo ok");
    if (p) pclose(p);
}

// Variant 5: strdup-like with fopen for temp file
static FILE* create_temp() {
    FILE* f = tmpfile();
    if (!f) return NULL;
    fprintf(f, "header\n");
    return f;  // DTT: tmpfile with no fclose
}

void variant5_tmpfile_wrapper() {
    FILE* f = create_temp();
    if (f) fclose(f);
}

// Variant 6: if/else mutually exclusive open() (giflib EGifOpenFileName pattern)
// DTT may see two open() calls on the same variable and flag as fd overwrite,
// not understanding only one branch executes.
static int open_exclusive_or_trunc(const char* path, int exclusive) {
    int fd;
    if (exclusive)
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    else
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd == -1) return -1;
    return fd;  // DTT: fd resource not released in this scope
}

void variant6_if_else_open() {
    int fd = open_exclusive_or_trunc("/dev/null", 0);
    if (fd >= 0) close(fd);
}

int main() {
    variant1_fopen_wrapper();
    variant2_open_fdopen_wrapper();
    variant3_malloc_init_wrapper();
    variant4_popen_wrapper();
    variant5_tmpfile_wrapper();
    variant6_if_else_open();
    return 0;
}
