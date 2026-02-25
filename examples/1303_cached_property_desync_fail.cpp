// 1303_cached_property_desync_fail.cpp
// Tests: Cached property binding catches reverse-scan bindings.
// When a scalar is assigned BEFORE malloc, the reverse scan binds it
// when the property is set. Realloc then correctly marks it stale.

#include <cstdlib>
#include <cstring>

int fake_read(int fd, void* buf, size_t count);
void send_data(const char* buf, size_t len);

// =============================================================================
// VARIANT 1: Cached bufsize stale after realloc (reverse scan binding)
// bufsize was the original allocation size. After realloc to smaller,
// bufsize is stale and used as a read limit.
// =============================================================================
void test_bufsize_stale(int fd) {
    size_t bufsize = 1024;
    char* buf = (char*)malloc(bufsize);  // buf._size_bytes = Var("bufsize") → reverse scan binds bufsize
    if (!buf) return;
    int n = fake_read(fd, buf, bufsize);
    if (n <= 0) { free(buf); return; }

    // Shrink buffer
    char* tmp = (char*)realloc(buf, 256);
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // buf._size_bytes = 256, but bufsize is still 1024 — DESYNC!

    // Read again with stale bufsize
    fake_read(fd, buf, bufsize);       // expected-error {{DTT: 'bufsize' is stale}}
    free(buf);
}

int main() { return 0; }
