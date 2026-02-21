// 1703b: Ownership transfer via return - remaining false positives
// These patterns involve fd-wrapping (fdopen) or path-insensitive branching
// that DTT cannot yet handle.

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

// Variant 2: open + fdopen wrapper (giflib EGifOpenFileName pattern)
// fd acquired then wrapped — fdopen consumes fd but DTT sees overwrite
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

// Variant 6: if/else mutually exclusive open() (giflib EGifOpenFileName pattern)
// DTT sees two open() calls on the same variable and flags as fd overwrite,
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
    variant2_open_fdopen_wrapper();
    variant6_if_else_open();
    return 0;
}
