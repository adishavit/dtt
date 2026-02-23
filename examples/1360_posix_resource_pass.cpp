// 1360: Integer fd scope_exit — safe patterns
// POSIX fd-returning functions use `when result._value >= 0` deferred guards.
// `if (fd >= 0)` resolves the guard, enabling @fd tracking and scope_exit checks.

#include <cstdlib>
#include <unistd.h>
#include <sys/epoll.h>

// Variant 1: mkstemp() with proper close
void variant1_mkstemp_closed() {
    char tmpl[] = "/tmp/dtt_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd >= 0) {
        write(fd, "test", 4);
        close(fd);
    }
}

// Variant 2: epoll_create1() with proper close
void variant2_epoll_closed() {
    int efd = epoll_create1(0);
    if (efd >= 0) {
        close(efd);
    }
}

// Variant 3: epoll_create() with proper close
void variant3_epoll_create_closed() {
    int efd = epoll_create(10);
    if (efd >= 0) {
        close(efd);
    }
}

int main() { return 0; }
