// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 891_compound_memcpy_size_xfail.cpp
// Tests: Compound expressions in memcpy SIZE argument should be tracked
// Bug: getSizeValue uses getExprPath which doesn't handle len+10
// These should produce errors but currently don't (size is UNKNOWN, check skips)

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Variant 1: memcpy with compound size, obvious overflow
void test_memcpy_compound_size_overflow(size_t len) {
    char buf[100];
    // Copy len + 200 bytes - always overflows 100-byte buffer
    memcpy(buf, "x", len + 200);  // SHOULD ERROR: (len + 200) > 100 for any len >= 0  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
}

// Variant 2: memcpy with compound size, unverified
void test_memcpy_compound_size_unverified(size_t len) {
    char buf[100];
    // Copy len + 10 bytes - can't prove fits in 100
    memcpy(buf, "x", len + 10);  // SHOULD ERROR: can't prove (len + 10) <= 100  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
}

// Variant 3: memset with compound size
void test_memset_compound_size(size_t n) {
    char buf[64];
    memset(buf, 0, n * 2);  // SHOULD ERROR: can't prove (n * 2) <= 64  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
}

// Variant 4: Both allocation AND copy size are compound
void test_both_compound(size_t len) {
    char* buf = static_cast<char*>(malloc(len + 10));
    if (!buf) return;
    // Copy len + 20 into buffer of len + 10 - always overflows by 10
    memcpy(buf, "x", len + 20);  // SHOULD ERROR: (len + 20) > (len + 10)  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

int main() { return 0; }
