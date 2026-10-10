// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 884_memcpy_src_unverified_fail.cpp
// Tests: Source-side unverified bounds checking (CWE-126)
// DTT warns when source over-read cannot be proven safe

#include <cstddef>
#include <cstring>

// Variant 1: Destination safe, source unverified
// Source-side uncertainty is reported in enforce mode.
void test_src_unverified_dst_safe(size_t n) {
    char dst[32];
    char src[16];
    char* p = src + 4;  // 12 bytes remain
    if (n <= sizeof(dst)) {
        memcpy(dst, p, n);  // dst: OK (n <= 32), src: SHOULD WARN (n vs 12 unproven)  // dtt-error {{CWE-126}}
    }
}

// Variant 2: Heartbleed-like pattern
// Large destination, small source, untrusted length
void test_heartbleed_like(size_t claimed_len) {
    unsigned char record[16];
    unsigned char* payload = record + 3;  // 13 bytes remain
    unsigned char out[1024];  // Large destination
    if (claimed_len <= sizeof(out)) {
        memcpy(out, payload, claimed_len);  // dst: OK, src: SHOULD WARN  // dtt-error {{CWE-126}}
    }
}

// Variant 3: Both source and destination should warn
void test_both_unverified(size_t n) {
    char src[16];
    char dst[16];
    memcpy(dst, src, n);  // Both sides unproved. // dtt-error {{memcpy size fits.*destination}} // dtt-error {{CWE-126}}
}

int main() { return 0; }
