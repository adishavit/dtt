// 812_strcpy_pass.cpp
// Tests: Safe strcpy/strncpy usage that should compile without DTT errors

#include <cstring>
#include <cstddef>
#include <cstdio>

// =============================================================================
// SECTION 1: Known-length string literals
// =============================================================================

// Variant 1.1: literal fits in buffer
void test_literal_fits() {
    char buf[20];
    strcpy(buf, "hello");  // OK: 6 <= 20 (5 chars + null)
}

// Variant 1.2: literal exactly fits
void test_literal_exact() {
    char buf[6];
    strcpy(buf, "hello");  // OK: 6 == 6
}

// Variant 1.3: empty string
void test_empty_literal() {
    char buf[10];
    strcpy(buf, "");  // OK: 1 <= 10 (just null)
}

// =============================================================================
// SECTION 2: strlen guard before strcpy
// =============================================================================

// Variant 2.1: strlen guard
void test_strlen_guard(const char* src) {
    char buf[100];
    if (strlen(src) < sizeof(buf)) {
        strcpy(buf, src);  // OK: strlen(src) + 1 <= 100
    }
}

// Variant 2.2: strlen guard with early return
void test_strlen_early_return(const char* src) {
    char buf[64];
    if (strlen(src) >= sizeof(buf)) return;
    strcpy(buf, src);  // OK: guarded
}

// Variant 2.3: strlen saved to variable
void test_strlen_variable(const char* src) {
    char buf[128];
    size_t len = strlen(src);
    if (len < sizeof(buf)) {
        strcpy(buf, src);  // OK: len < 128
    }
}

// =============================================================================
// SECTION 3: strncpy (bounded copy)
// =============================================================================

// Variant 3.1: strncpy with sizeof
void test_strncpy_sizeof(const char* src) {
    char buf[50];
    strncpy(buf, src, sizeof(buf));  // OK: 50 <= 50
    buf[sizeof(buf) - 1] = '\0';     // Ensure null-termination
}

// Variant 3.2: strncpy with explicit size
void test_strncpy_explicit(const char* src) {
    char buf[100];
    strncpy(buf, src, 100);  // OK: 100 <= 100
}

// Variant 3.3: strncpy partial copy
void test_strncpy_partial(const char* src) {
    char buf[100];
    strncpy(buf, src, 50);  // OK: 50 <= 100
}

// =============================================================================
// SECTION 4: strcat with space check
// =============================================================================

// Variant 4.1: strcat with known sizes
void test_strcat_known() {
    char buf[20] = "hello";  // strlen = 5
    strcat(buf, " world");   // OK: 5 + 6 + 1 = 12 <= 20
}

// Variant 4.2: strcat with explicit capacity check
void test_strcat_guard(const char* suffix) {
    char buf[100] = "prefix";
    size_t current_len = strlen(buf);
    size_t suffix_len = strlen(suffix);
    if (current_len + suffix_len + 1 <= sizeof(buf)) {
        strcat(buf, suffix);  // OK: guarded
    }
}

// =============================================================================
// SECTION 5: strncat (bounded append)
// =============================================================================

// Variant 5.1: strncat with remaining space
void test_strncat_remaining() {
    char buf[100] = "hello";
    strncat(buf, " world this is long", sizeof(buf) - strlen(buf) - 1);
    // OK: explicitly limits to remaining space
}

// Variant 5.2: strncat with fixed limit
void test_strncat_fixed() {
    char buf[50] = "";
    strncat(buf, "some text", 10);  // OK: at most 10 chars added
}

// =============================================================================
// SECTION 6: sprintf alternatives
// =============================================================================

// Variant 6.1: snprintf is safe
void test_snprintf(int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "value: %d", value);  // OK: bounded
}

// Variant 6.2: snprintf with explicit size
void test_snprintf_explicit(const char* name) {
    char buf[64];
    snprintf(buf, 64, "Hello, %s!", name);  // OK: 64 <= 64
}

// =============================================================================
// SECTION 7: Pointer arithmetic with strcpy
// =============================================================================

// Variant 7.1: strcpy to offset position (known sizes)
void test_strcpy_offset() {
    char buf[100] = "prefix: ";  // 8 chars
    strcpy(buf + 8, "data");     // OK: 5 <= 92
}

// Variant 7.2: strncpy to offset with remaining
void test_strncpy_offset(const char* src) {
    char buf[100] = "prefix: ";
    size_t offset = strlen(buf);
    strncpy(buf + offset, src, sizeof(buf) - offset);  // OK
}

// =============================================================================
// SECTION 8: Copy to struct member
// =============================================================================

// Variant 8.1: strcpy to struct member with known literal
void test_struct_strcpy() {
    struct S { char name[32]; int id; };
    S s;
    strcpy(s.name, "short");  // OK: 6 <= 32
}

// Variant 8.2: strncpy to struct member
void test_struct_strncpy(const char* name) {
    struct S { char name[32]; int id; };
    S s;
    strncpy(s.name, name, sizeof(s.name) - 1);
    s.name[sizeof(s.name) - 1] = '\0';  // OK: bounded
}

// =============================================================================
// SECTION 9: strlcpy (BSD safe copy)
// =============================================================================

#ifdef __APPLE__  // strlcpy is BSD-specific

// Variant 9.1: strlcpy basic usage
void test_strlcpy(const char* src) {
    char buf[100];
    strlcpy(buf, src, sizeof(buf));  // OK: always safe
}

// Variant 9.2: strlcat basic usage
void test_strlcat(const char* suffix) {
    char buf[100] = "prefix";
    strlcat(buf, suffix, sizeof(buf));  // OK: always safe
}

#endif

// =============================================================================
// SECTION 10: memcpy with strlen + 1
// =============================================================================

// Variant 10.1: memcpy with strlen guard
void test_memcpy_strlen(const char* src) {
    char buf[100];
    size_t len = strlen(src);
    if (len + 1 <= sizeof(buf)) {
        memcpy(buf, src, len + 1);  // OK: includes null, guarded
    }
}

// Variant 10.2: memcpy known literal
void test_memcpy_literal() {
    char buf[20];
    memcpy(buf, "hello", 6);  // OK: 6 <= 20
}

int main() {
    return 0;
}
