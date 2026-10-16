// 1752: File descriptors - a descriptor that is never closed leaks
#include <fcntl.h>
#include <unistd.h>

// Variant 1: never closed.
void variant1_never_closed() {
    int fd = open("a", O_RDONLY);  // dtt-error {{scope-exit|leak}}
    if (fd < 0) return;
}

// Variant 2: closed on one path only.
void variant2_closed_on_one_path(int c) {
    int fd = open("a", O_RDONLY);  // dtt-error {{scope-exit|leak}}
    if (fd < 0) return;
    if (c) close(fd);
}

// Variant 3: a second open overwrites the first descriptor.
void variant3_overwrite() {
    int fd = open("a", O_RDONLY);
    if (fd < 0) return;
    fd = open("b", O_RDONLY);  // dtt-error {{overwriting|leak}}
    if (fd >= 0) close(fd);
}

int main() { return 0; }
