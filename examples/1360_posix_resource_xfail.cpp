// 1360: XFAIL - Integer fd scope_exit tracking gap
// The libc.dtt annotations for fd-returning functions (mkstemp, epoll_create,
// etc.) use unconditional `ensures result.@fd` / `scope_exit result.@fd`
// because `when` guards require _nonnull proofs, which don't apply to int
// return types. DTT doesn't recognize `if (fd >= 0)` as a guard that could
// resolve scope_exit constraints, so the scope_exit fires unconditionally
// even though the fd is properly closed inside the guard.

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
