// 1360: Integer fd scope_exit — false positive
// DTT fires scope-exit violations on safe fd patterns because the `when` guard
// system requires `_nonnull` proofs, which don't apply to int returns.
// `if (fd >= 0)` can't resolve deferred guards. Needs integer-range guard support.

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
