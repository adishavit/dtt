// 1360: XFAIL - Integer fd deferred guard evaluation gap
// DTT's deferred guard evaluation works for pointer null checks (if (ptr)),
// but not for integer fd validity checks (if (fd >= 0)). The @fd ghost field
// is never applied because the `when` guard system requires _nonnull proofs.

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
