// 876_strfn_pass.cpp
// Tests: Safe string function usage that should NOT produce errors
// These patterns are correctly bounded - DTT should accept them

#include <cstring>
#include <cstdio>
#include <cstddef>

// =============================================================================
// SECTION 1: strncpy safe patterns
// =============================================================================

// Variant 1.1: strncpy with sizeof(dest)
void test_strncpy_sizeof() {
    char dest[100];
    const char* src = "hello world";
    strncpy(dest, src, sizeof(dest));  // OK: 100 <= 100
}

// Variant 1.2: strncpy with constant less than dest
void test_strncpy_const() {
    char dest[50];
    strncpy(dest, "test", 10);  // OK: 10 <= 50
}

// Variant 1.3: strncpy with guarded size
void test_strncpy_guarded(size_t n) {
    char dest[64];
    if (n <= sizeof(dest)) {
        strncpy(dest, "data", n);  // OK: guarded
    }
}

// =============================================================================
// SECTION 2: snprintf safe patterns
// =============================================================================

// Variant 2.1: snprintf with sizeof(dest)
void test_snprintf_sizeof() {
    char buf[128];
    snprintf(buf, sizeof(buf), "value: %d", 42);  // OK: 128 <= 128
}

// Variant 2.2: snprintf with constant
void test_snprintf_const() {
    char buf[256];
    snprintf(buf, 100, "test %s", "data");  // OK: 100 <= 256
}

// Variant 2.3: snprintf with guarded size
void test_snprintf_guarded(size_t n) {
    char buf[32];
    if (n <= sizeof(buf)) {
        snprintf(buf, n, "x");  // OK: guarded
    }
}

// =============================================================================
// SECTION 3: strcpy safe patterns (with string literals)
// =============================================================================

// Variant 3.1: strcpy with short literal
void test_strcpy_short() {
    char dest[20];
    strcpy(dest, "hello");  // OK: 6 <= 20 (5 chars + null)
}

// Variant 3.2: strcpy with exact fit
void test_strcpy_exact() {
    char dest[6];
    strcpy(dest, "hello");  // OK: 6 == 6 (exact fit)
}

// Variant 3.3: strcpy empty string
void test_strcpy_empty() {
    char dest[10];
    strcpy(dest, "");  // OK: 1 <= 10
}

int main() { return 0; }
