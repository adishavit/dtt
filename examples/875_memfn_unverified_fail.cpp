// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 875_memfn_unverified_fail.cpp
// Tests: Unguarded runtime sizes should produce warnings/errors
// These patterns lack bounds checks - DTT should catch them

#include <cstring>
#include <cstddef>

// Variant 1: Unguarded memcpy from parameter
void test_unguarded_memcpy(size_t n) {
    char buf[256];
    memcpy(buf, "data", n);  // ERROR: n is unguarded  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
}

// Variant 2: Unguarded memset
void test_unguarded_memset(size_t n) {
    char buf[64];
    memset(buf, 0, n);  // ERROR: n is unguarded  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
}

// Variant 3: Unguarded memcmp
void test_unguarded_memcmp(size_t n) {
    char a[10], b[20];
    memcmp(a, b, n);  // ERROR: n is unguarded  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
}

// Variant 4: Unguarded memchr
void test_unguarded_memchr(size_t n) {
    char buf[32];
    memchr(buf, 'x', n);  // ERROR: n is unguarded  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
}

// Variant 5: Network buffer pattern (Heartbleed-like)
void handle_request(const char* data, size_t claimed_len) {
    char buf[1024];
    memcpy(buf, data, claimed_len);  // ERROR: claimed_len unguarded  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
}

int main() { return 0; }
