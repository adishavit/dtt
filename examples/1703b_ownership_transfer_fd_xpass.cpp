// 1703b: Ownership transfer via return - remaining false positive
// The fdopen wrapper (formerly variant 2) passes: 1752_fd_resource_pass.cpp.

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

// Variant 6: if/else mutually exclusive open() (giflib EGifOpenFileName pattern)
// open() promises a descriptor when the result is >= 0; `fd == -1` does not
// prove that, so in enforce mode fd counts as never checked.
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
    variant6_if_else_open();
    return 0;
}
