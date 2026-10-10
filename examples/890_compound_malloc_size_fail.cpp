// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 890_compound_malloc_size_xfail.cpp
// Tests: Compound allocation expressions like malloc(len + 10) should be tracked
// Bug: getExprPath() only handles simple variables, so malloc(len + 10) results
//      in unknown size, causing mem* checks to silently skip (false negatives)
// These should all produce DTT errors but currently don't

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Variant 1: malloc(len + 10) with obvious overflow - SHOULD ERROR
void test_compound_obvious_overflow(size_t len) {
    char* buf = static_cast<char*>(malloc(len + 10));
    if (!buf) return;
    // Copy len + 100 bytes into buffer of size len + 10
    // This is clearly wrong: (len + 100) > (len + 10) for all len
    memcpy(buf, "x", len + 100);  // SHOULD ERROR: proven overflow  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

// Variant 2: malloc(len * 2) unguarded - SHOULD ERROR (unverified)
void test_compound_multiply_unguarded(size_t len) {
    char* buf = static_cast<char*>(malloc(len * 2));
    if (!buf) return;
    memcpy(buf, "hello world!", 13);  // SHOULD ERROR: can't prove len*2 >= 13  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

// Variant 3: malloc(len + offset) unguarded - SHOULD ERROR (unverified)
void test_compound_add_unguarded(size_t len, size_t offset) {
    char* buf = static_cast<char*>(malloc(len + offset));
    if (!buf) return;
    memset(buf, 0, 64);  // SHOULD ERROR: can't prove len + offset >= 64  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

// Variant 4: realloc to compound size - SHOULD ERROR
void test_realloc_compound_unguarded(size_t old_len, size_t extra) {
    char* buf = static_cast<char*>(malloc(old_len));
    if (!buf) return;
    buf = static_cast<char*>(realloc(buf, old_len + extra));
    if (!buf) return;
    memcpy(buf, "0123456789", 10);  // SHOULD ERROR: can't prove old_len + extra >= 10  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

int main() { return 0; }
