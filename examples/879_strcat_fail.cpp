// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 879_strcat_fail.cpp
// Tests: Unsafe strcat/strncat usage that DTT should catch

#include <cstring>
#include <cstddef>

// =============================================================================
// SECTION 1: strcat overflow with string literals
// =============================================================================

// Variant 1.1: strcat literal too long for buffer
void test_strcat_overflow() {
    char dest[5] = "";
    strcat(dest, "hello");  // ERROR: 0 + 5 + 1 = 6 > 5  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 1.2: strcat long literal
void test_strcat_long() {
    char dest[10] = "";
    strcat(dest, "this is way too long");  // ERROR: 21 > 10  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 1.3: strcat with unverified source
void test_strcat_unverified(const char* src) {
    char dest[32] = "";
    strcat(dest, src);  // ERROR: src length cannot be verified  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// =============================================================================
// SECTION 2: strncat overflow
// =============================================================================

// Variant 2.1: strncat n exceeds remaining space
void test_strncat_overflow() {
    char dest[10] = "";
    strncat(dest, "source", 20);  // ERROR: 0 + 20 + 1 = 21 > 10  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 2.2: strncat with unverified n
void test_strncat_unverified_n(size_t n) {
    char dest[50] = "";
    strncat(dest, "data", n);  // ERROR: n is unguarded  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 2.3: strncat unverified source (even with n)
void test_strncat_unverified_src(const char* src) {
    char dest[64] = "";
    strncat(dest, src, 100);  // ERROR: min(100, strlen(src)) + 1 could exceed 64  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

int main() { return 0; }
