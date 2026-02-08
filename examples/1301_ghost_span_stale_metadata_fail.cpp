// 1301_ghost_span_stale_metadata_fail.cpp
// Tests: Ghost span binding detects stale length variables after realloc.
// DTT binds scalar variables to buffer _size_bytes when they share the same
// symbolic value at assignment time. When realloc changes the buffer size,
// DTT marks bound scalars as stale and reports an error when they're used.

#include <cstdlib>
#include <cstring>

// Simulated opaque functions (DTT can't see inside)
void send_response(const char* data, size_t len);
char* allocate_peer_buffer(size_t len);

// =============================================================================
// VARIANT 1: Stale length sent as protocol metadata (Heartbleed pattern)
// The stale length is passed to an opaque function after realloc.
// =============================================================================
void test_heartbleed_metadata(size_t n, size_t m) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    size_t len = n;                // len bound to buf._size_bytes

    char* tmp = (char*)realloc(buf, m);  // buf._size_bytes changes to m
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // len is still n — DESYNC!

    send_response(buf, len);       // expected-error {{DTT: 'len' is stale}}
    free(buf);
}

// =============================================================================
// VARIANT 2: Stale length used to allocate another buffer
// =============================================================================
void test_stale_alloc_size(size_t n) {
    char* src = (char*)malloc(n);
    if (!src) return;
    size_t src_len = n;            // src_len bound to src._size_bytes

    char* tmp = (char*)realloc(src, n / 2);  // src._size_bytes changes to n/2
    if (!tmp) { free(src); return; }
    src = tmp;
    // src_len is still n — DESYNC!

    char* dst = (char*)malloc(src_len);  // expected-error {{DTT: 'src_len' is stale}}
    if (!dst) { free(src); return; }

    free(src);
    free(dst);
}

// =============================================================================
// VARIANT 3: Stale length used in comparison/logic (not bounds check)
// =============================================================================
void test_stale_length_comparison(size_t n, size_t threshold) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    size_t len = n;                // len bound to buf._size_bytes

    char* tmp = (char*)realloc(buf, n * 4);  // buf._size_bytes changes to n*4
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // len is still n — DESYNC!

    if (len < threshold) {
        send_response(buf, len);   // expected-error {{DTT: 'len' is stale}}
    }
    free(buf);
}

int main() { return 0; }
