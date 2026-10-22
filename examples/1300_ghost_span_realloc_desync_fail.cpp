// 1300_ghost_span_realloc_desync_fail.cpp
// Tests: External length variable desyncs from buffer after realloc
// DTT catches these via Z3 symbolic bounds checking — after realloc(buf, m),
// buf._size_bytes = m, and when memset/index uses len (still n), Z3 can't
// prove m >= n → error.
//
// CVE examples: CVE-2014-0160 (Heartbleed), CVE-2023-38545 (curl SOCKS5),
//               CVE-2021-3156 (sudo Baron Samedit)

#include <cstdlib>
#include <cstring>

// =============================================================================
// VARIANT 1: Basic realloc desync — len not updated after realloc
// =============================================================================
void test_realloc_desync_basic(size_t n, size_t m) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    size_t len = n;              // len tracks buf._size

    buf = (char*)realloc(buf, m);  // buf._size is now m
    if (!buf) return;
    // len is still n — DESYNC!

    memset(buf, 0, len);  // BUG: if m < n, writes past end of buf
    free(buf);
}

// =============================================================================
// VARIANT 2: Separate capacity variable goes stale
// =============================================================================
void test_realloc_desync_capacity(size_t initial) {
    int* data = (int*)malloc(initial * sizeof(int));
    if (!data) return;
    size_t capacity = initial;  // capacity tracks data._size

    // Grow the buffer
    size_t new_cap = initial / 2;  // Shrink!
    int* tmp = (int*)realloc(data, new_cap * sizeof(int));
    if (!tmp) { free(data); return; }
    data = tmp;
    // capacity is still 'initial' — DESYNC!

    for (size_t i = 0; i < capacity; ++i) {
        data[i] = (int)i;  // BUG: writes past new_cap when i >= new_cap
    }
    free(data);
}

// =============================================================================
// VARIANT 3: Length variable in struct, buffer reallocated externally
// =============================================================================
struct Buffer {
    char* data;
    size_t len;
};

void test_struct_field_desync(size_t n) {
    Buffer b;
    b.data = (char*)malloc(n);
    if (!b.data) return;
    b.len = n;  // b.len tracks b.data._size

    // Reallocate but forget to update len
    char* tmp = (char*)realloc(b.data, n / 2);
    if (!tmp) { free(b.data); return; }
    b.data = tmp;
    // b.len is still n, b.data._size is now n/2 — DESYNC!

    memset(b.data, 'A', b.len);  // BUG: writes n bytes into n/2 buffer
    free(b.data);
}

// =============================================================================
// VARIANT 4: Protocol-style — external length field trusted over actual alloc
// (Heartbleed pattern)
// =============================================================================
struct HeartbeatMsg {
    unsigned short payload_length;  // claimed length from network
    char payload[1];                // flexible array member
};

void test_heartbleed_pattern(const char* input, size_t input_len) {
    if (input_len < sizeof(unsigned short)) return;

    // Parse the claimed payload length from the message
    unsigned short claimed_len;
    memcpy(&claimed_len, input, sizeof(claimed_len));

    // Allocate response buffer based on claimed length (WRONG!)
    char* response = (char*)malloc(claimed_len);
    if (!response) return;

    // The actual available payload may be much smaller than claimed_len
    // but we trust the external length variable
    size_t actual_payload = input_len - sizeof(unsigned short);
    (void)actual_payload;  // This is the REAL size we should use

    // Copy using the CLAIMED length — classic ghost span desync
    memcpy(response, input + sizeof(unsigned short), claimed_len);
    // BUG: if claimed_len > actual_payload, reads past input buffer

    free(response);
}

// =============================================================================
// VARIANT 5: Multiple variables tracking same buffer, only one updated
// =============================================================================
void test_multi_alias_desync(size_t n) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    size_t len = n;
    size_t cap = n;

    // Grow
    char* tmp = (char*)realloc(buf, n * 2);
    if (!tmp) { free(buf); return; }
    buf = tmp;
    cap = n * 2;  // Updated!
    // len is still n — but cap is correct

    // Use the WRONG size variable for the copy
    memset(buf, 0, cap);  // OK: cap was updated
    memset(buf, 0, len);  // Would be OK only if len <= cap, but semantically wrong

    // Now shrink
    tmp = (char*)realloc(buf, n / 2);
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // Neither len nor cap updated — BOTH DESYNCED!

    memset(buf, 0, len);  // BUG: len (n) > actual size (n/2)
    free(buf);
}

int main() { return 0; }
