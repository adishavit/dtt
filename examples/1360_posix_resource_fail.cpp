// 1360: Resource leaks for mkstemp and epoll file descriptors
// Tests scope-exit constraints for fd-returning functions

#include <cstdlib>
#include <unistd.h>
#include <sys/epoll.h>

// Variant 1: mkstemp() fd leak
void variant1_mkstemp_leak() {
    char tmpl[] = "/tmp/dtt_XXXXXX";
    int fd = mkstemp(tmpl);  // Acquires @fd  dtt-error {{resource pointer that is never null-checked}}
    // BUG: fd never closed - resource leak
    (void)fd;
}

// Variant 2: epoll_create1() fd leak
void variant2_epoll_leak() {
    int efd = epoll_create1(0);  // Acquires @fd  dtt-error {{resource pointer that is never null-checked}}
    // BUG: efd never closed - resource leak
    (void)efd;
}

// Variant 3: epoll_create() fd leak (legacy)
void variant3_epoll_create_leak() {
    int efd = epoll_create(10);  // Acquires @fd  dtt-error {{resource pointer that is never null-checked}}
    // BUG: efd never closed - resource leak
    (void)efd;
}

int main() { return 0; }
