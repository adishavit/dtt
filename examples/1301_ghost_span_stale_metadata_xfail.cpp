// 1301_ghost_span_stale_metadata_xfail.cpp
// Tests: Stale length variable used as METADATA, not for direct buffer access.
// DTT catches direct memset/memcpy/index bounds violations via Z3, but misses
// cases where the stale length is used indirectly — passed to functions, used
// to allocate other buffers, or sent as protocol metadata.
//
// THIS is the real ghost span gap. The Heartbleed pattern: the stale length
// is sent over the wire as a claimed payload size, not used for local memcpy.
//
// Ghost span fix: bind len ←→ buf._size at allocation time. When buf._size
// changes (realloc), DTT flags len as stale IMMEDIATELY — not just when
// len is used for a bounds check.

#include <cstdlib>
#include <cstring>

// Simulated network send (DTT can't see inside)
void send_response(const char* data, size_t len);
// Simulated allocation
char* allocate_peer_buffer(size_t len);

// =============================================================================
// VARIANT 1: Stale length sent as protocol metadata (Heartbleed pattern)
// The bug: we tell the peer "I have len bytes" but buf only has m bytes.
// DTT doesn't flag this because send_response is opaque — no bounds check.
// =============================================================================
void test_heartbleed_metadata(size_t n, size_t m) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    size_t len = n;                // len tracks buf._size

    char* tmp = (char*)realloc(buf, m);  // buf._size is now m
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // len is still n — DESYNC!

    // Ghost span would flag: len is stale (was bound to buf._size = n, now m)
    send_response(buf, len);       // BUG: tells peer we have n bytes, only have m
    free(buf);
}

// =============================================================================
// VARIANT 2: Stale length used to allocate another buffer
// The second buffer is sized based on the stale length of the first.
// =============================================================================
void test_stale_alloc_size(size_t n) {
    char* src = (char*)malloc(n);
    if (!src) return;
    size_t src_len = n;

    // Shrink src
    char* tmp = (char*)realloc(src, n / 2);
    if (!tmp) { free(src); return; }
    src = tmp;
    // src_len is still n, src._size is n/2 — DESYNC!

    // Allocate dest based on STALE src_len
    char* dst = (char*)malloc(src_len);  // Allocates n bytes when n/2 would suffice
    if (!dst) { free(src); return; }
    // Not a security bug here (over-allocation), but if logic depends on
    // src_len == src._size, downstream code breaks.

    // Now the inverse — under-allocation:
    // If src grew and src_len wasn't updated:
    free(src);
    free(dst);
}

// =============================================================================
// VARIANT 3: Stale length used in comparison/logic (not bounds check)
// =============================================================================
void test_stale_length_comparison(size_t n, size_t threshold) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    size_t len = n;

    // Grow buffer
    char* tmp = (char*)realloc(buf, n * 4);
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // len is still n, buf._size is n*4 — DESYNC!

    // Decision based on stale length
    if (len < threshold) {
        // Ghost span: len is stale — this branch may be wrong
        // If n < threshold but n*4 >= threshold, we're in the wrong branch
        send_response(buf, len);  // BUG: wrong amount sent
    }
    free(buf);
}

// =============================================================================
// VARIANT 4: Stale length stored into struct field, propagated elsewhere
// =============================================================================
struct Message {
    char* payload;
    size_t payload_len;  // Should track payload._size
};

void send_message(Message* msg);

void test_struct_propagation(size_t n) {
    Message msg;
    msg.payload = (char*)malloc(n);
    if (!msg.payload) return;
    msg.payload_len = n;

    // Reallocate payload but forget to update payload_len
    char* tmp = (char*)realloc(msg.payload, n / 2);
    if (!tmp) { free(msg.payload); return; }
    msg.payload = tmp;
    // msg.payload_len is still n — DESYNC!

    // Pass the whole struct — stale length propagates
    send_message(&msg);  // BUG: msg.payload_len > actual payload size

    free(msg.payload);
}

// =============================================================================
// VARIANT 5: Length used as realloc argument (circular desync)
// "Double the buffer" using stale length instead of actual size
// =============================================================================
void test_realloc_from_stale_length(size_t n) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    size_t len = n;

    // First grow
    char* tmp = (char*)realloc(buf, n * 2);
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // buf._size = n*2, len = n — DESYNC!

    // "Double the buffer" using stale len
    tmp = (char*)realloc(buf, len * 2);  // BUG: reallocates to 2n, which is SAME
    if (!tmp) { free(buf); return; }     // as current size, not 4n as intended
    buf = tmp;

    // Developer thinks buf is now 4n, but it's only 2n
    // Any access in range [2n, 4n) would be OOB
    free(buf);
}

int main() { return 0; }
