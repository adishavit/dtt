// 1304_return_value_binding_fail.cpp
// Tests: Return value binding detects staleness after buffer mutation.
// Pattern: int n = f(buf, size) where size matches buf._size_bytes.
// When buf is reallocated, n becomes stale.

#include <cstdlib>
#include <cstring>

int fake_read(int fd, void* buf, size_t count);
void send_data(const char* buf, size_t len);

// =============================================================================
// VARIANT 1: read() return is buffer's populated size — stale after realloc
// The classic: n = read(fd, buf, size). buf is reallocated, n is stale.
// =============================================================================
void test_read_return_stale(int fd) {
    char* buf = (char*)malloc(1024);
    if (!buf) return;
    int n = fake_read(fd, buf, 1024);  // n = bytes actually in buf
    if (n <= 0) { free(buf); return; }

    // Shrink buffer — n is now larger than buf
    char* tmp = (char*)realloc(buf, 256);
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // buf._size_bytes = 256, but n could be up to 1024 — DESYNC!

    send_data(buf, n);                 // expected-error {{DTT: 'n' is stale}}
    free(buf);
}

int main() { return 0; }
