// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 877_strfn_fail.cpp
// Tests: Unsafe string function usage that DTT should catch
// These patterns have buffer overflows - DTT must detect them

#include <cstring>
#include <cstdio>
#include <cstddef>

// =============================================================================
// SECTION 1: strncpy overflow
// =============================================================================

// Variant 1.1: strncpy size exceeds dest
void test_strncpy_overflow() {
    char dest[10];
    strncpy(dest, "source", 50);  // ERROR: 50 > 10  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 1.2: strncpy sizeof(src) > sizeof(dest)
void test_strncpy_wrong_sizeof() {
    char dest[20];
    char src[100];
    strncpy(dest, src, sizeof(src));  // ERROR: 100 > 20  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 1.3: strncpy unguarded runtime size
void test_strncpy_unguarded(size_t n) {
    char dest[64];
    strncpy(dest, "data", n);  // ERROR: n is unguarded  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// =============================================================================
// SECTION 2: snprintf overflow
// =============================================================================

// Variant 2.1: snprintf size exceeds buffer
void test_snprintf_overflow() {
    char buf[16];
    snprintf(buf, 32, "data: %d", 42);  // ERROR: 32 > 16 // dtt-error {{CWE-120}}
}

// Variant 2.2: snprintf unguarded runtime size
void test_snprintf_unguarded(size_t n) {
    char buf[50];
    snprintf(buf, n, "format");  // KNOWN GAP (not asserted): n is unguarded
}

// =============================================================================
// SECTION 3: sprintf (always unsafe)
// =============================================================================

// Variant 3.1: basic sprintf
void test_sprintf_basic() {
    char buf[100];
    sprintf(buf, "hello %s", "world");  // KNOWN GAP (not asserted): sprintf is unbounded
}

// Variant 3.2: sprintf with integer
void test_sprintf_int() {
    char buf[50];
    sprintf(buf, "value: %d", 42);  // KNOWN GAP (not asserted): sprintf is unbounded
}

// =============================================================================
// SECTION 4: strcpy overflow
// =============================================================================

// Variant 4.1: strcpy literal too long
void test_strcpy_overflow() {
    char dest[5];
    strcpy(dest, "hello");  // ERROR: 6 > 5 (5 chars + null)  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 4.2: strcpy long literal
void test_strcpy_long() {
    char dest[10];
    strcpy(dest, "this is way too long for the buffer");  // ERROR: 36 > 10  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 4.3: strcpy unverified source
void test_strcpy_unverified(const char* src) {
    char dest[32];
    strcpy(dest, src);  // ERROR: src length cannot be verified  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

// Variant 4.4: strcpy off-by-one
void test_strcpy_off_by_one() {
    char dest[4];
    strcpy(dest, "test");  // ERROR: 5 > 4 (4 chars + null)  // dtt-error {{(?:strcpy|strncpy|strcat|strncat|snprintf).*(?:buffer|length)}}
}

int main() { return 0; }
