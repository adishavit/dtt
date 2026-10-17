// 1752: File descriptors - one open() call is one resource
//
// libc.dtt declares open(2) and open(3). DTT used to register both contracts
// for a single call, then reported the second registration as overwriting
// (leaking) the first, on every `int fd = open(...)` in C++.
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

// Variant 1: close when open succeeded.
void variant1_close_if_open() {
    int fd = open("a", O_RDONLY);
    if (fd >= 0) close(fd);
}

// Variant 2: early return on failure.
void variant2_early_return() {
    int fd = open("a", O_RDONLY);
    if (fd < 0) return;
    close(fd);
}

// Variant 3: report the failure, close on success.
void variant3_report_then_close() {
    int fd = open("a", O_RDONLY);
    if (fd < 0) perror("open");
    if (fd >= 0) close(fd);
}

// Variant 4: descriptor declared in the if statement.
void variant4_if_init() {
    if (int fd = open("a", O_RDONLY); fd >= 0) {
        close(fd);
    }
}

// Variant 5: the three-argument form.
void variant5_create() {
    int fd = open("a", O_WRONLY | O_CREAT, 0644);
    if (fd < 0) return;
    close(fd);
}

// Variant 6: open + fdopen wrapper (giflib EGifOpenFileName pattern), from 1703b
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

int main() { return 0; }
