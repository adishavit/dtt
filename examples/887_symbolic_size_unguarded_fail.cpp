// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 887_symbolic_size_unguarded_fail.cpp
// Tests: Symbolic buffer sizes without guards should trigger DTT errors
// Change 2 + 3: DTT now attempts proof for symbolic sizes and tracks them

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Variant 1: Symbolic destination, constant source - ERROR
void test_symbolic_dst_unguarded(size_t len) {
    char* buf = static_cast<char*>(malloc(len));
    if (!buf) return;
    memcpy(buf, "hello world!", 13);  // ERROR: can't prove len >= 13  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

// Variant 2: Both symbolic - ERROR for both dst and src
void test_both_symbolic_unguarded(size_t dst_len, size_t src_len, size_t copy_len) {
    char* dst = static_cast<char*>(malloc(dst_len));
    char* src = static_cast<char*>(malloc(src_len));
    if (!dst || !src) { free(dst); free(src); return; }
    memcpy(dst, src, copy_len);  // ERROR: can't prove copy_len <= dst or src  // dtt-error {{memcpy size fits.*destination}} // dtt-error {{CWE-126}}
    free(dst);
    free(src);
}

// Variant 3: Symbolic with potential overflow - ERROR
void test_symbolic_obvious_overflow(size_t len) {
    char* buf = static_cast<char*>(malloc(len));
    if (!buf) return;
    memcpy(buf, "x", len + 100);  // ERROR: can't prove (len+100) <= len  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

// Variant 4: memset with symbolic buffer - ERROR
void test_memset_symbolic_unguarded(size_t len) {
    char* buf = static_cast<char*>(malloc(len));
    if (!buf) return;
    memset(buf, 0, 64);  // ERROR: can't prove len >= 64  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

int main() { return 0; }
