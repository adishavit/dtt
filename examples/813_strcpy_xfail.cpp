// 813_strcpy_xfail.cpp
// Tests: Unsafe strcpy/strncpy usage that DTT SHOULD catch (CVE patterns)

#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdarg>

// =============================================================================
// SECTION 1: Literal overflow
// =============================================================================

// Variant 1.1: literal longer than buffer
void test_literal_overflow() {
    char buf[5];
    strcpy(buf, "hello");  // ERROR: 6 > 5 (5 chars + null)
}

// Variant 1.2: off-by-one with null
void test_literal_off_by_one() {
    char buf[5];
    strcpy(buf, "hell");  // OK: 5 == 5
    strcpy(buf, "hello");  // ERROR: 6 > 5
}

// Variant 1.3: long literal
void test_long_literal() {
    char buf[10];
    strcpy(buf, "this is a very long string");  // ERROR: 27 > 10
}

// =============================================================================
// SECTION 2: Unguarded strcpy (Moonlight CVE pattern)
// =============================================================================

// Variant 2.1: strcpy from parameter without check
void test_unguarded_param(const char* user_input) {
    char buf[256];
    strcpy(buf, user_input);  // ERROR: can't prove strlen(user_input) <= 255
}

// Variant 2.2: strcpy from unknown source
void test_unknown_source(const char* server_version) {
    char versionString[64];
    strcpy(versionString, server_version);  // ERROR: unguarded
}

// Variant 2.3: strcpy from network data
void test_network_data(const char* hostname) {
    char buf[255];
    strcpy(buf, hostname);  // ERROR: hostname could be any length
}

// =============================================================================
// SECTION 3: strncpy size exceeds buffer
// =============================================================================

// Variant 3.1: strncpy n > buffer
void test_strncpy_overflow(const char* src) {
    char buf[50];
    strncpy(buf, src, 100);  // ERROR: 100 > 50
}

// Variant 3.2: strncpy wrong sizeof
void test_strncpy_wrong_sizeof(const char* src) {
    char dest[32];
    char large[128];
    strncpy(dest, src, sizeof(large));  // ERROR: 128 > 32
}

// =============================================================================
// SECTION 4: strcat overflow
// =============================================================================

// Variant 4.1: strcat overflow with known sizes
void test_strcat_overflow() {
    char buf[10] = "hello";  // 5 chars
    strcat(buf, " world");   // ERROR: 5 + 6 + 1 = 12 > 10
}

// Variant 4.2: strcat unguarded
void test_strcat_unguarded(const char* suffix) {
    char buf[100] = "prefix: ";
    strcat(buf, suffix);  // ERROR: suffix length unknown
}

// Variant 4.3: multiple strcat overflow
void test_multiple_strcat() {
    char buf[20] = "";
    strcat(buf, "hello ");   // 7
    strcat(buf, "world ");   // 13
    strcat(buf, "foo ");     // 17
    strcat(buf, "bar");      // ERROR: 21 > 20
}

// =============================================================================
// SECTION 5: strncat overflow
// =============================================================================

// Variant 5.1: strncat wrong remaining calculation
void test_strncat_wrong_calc() {
    char buf[20] = "hello";  // 5 chars used
    strncat(buf, " world is great", 20);  // ERROR: might append 20 chars to 5
}

// Variant 5.2: strncat ignoring current length
void test_strncat_no_len_check(const char* s) {
    char buf[50] = "some prefix text ";  // ~17 chars
    strncat(buf, s, 40);  // ERROR: 17 + 40 + 1 could be > 50
}

// =============================================================================
// SECTION 6: sprintf (always unsafe)
// =============================================================================

// Variant 6.1: sprintf should always warn
void test_sprintf_warn(const char* name) {
    char buf[100];
    sprintf(buf, "Hello, %s!", name);  // ERROR/WARN: sprintf is unbounded
}

// Variant 6.2: sprintf with %s
void test_sprintf_string(const char* data) {
    char buf[64];
    sprintf(buf, "%s", data);  // ERROR: no bounds checking
}

// Variant 6.3: vsprintf equally bad
void log_message(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);  // ERROR: unbounded
    va_end(args);
}

// =============================================================================
// SECTION 7: snprintf size exceeds buffer
// =============================================================================

// Variant 7.1: snprintf n > buffer (CVE-2018-18820 adjacent)
void test_snprintf_overflow() {
    char buf[50];
    snprintf(buf, 100, "data");  // ERROR: 100 > 50
}

// Variant 7.2: snprintf wrong sizeof
void test_snprintf_wrong_sizeof(int x) {
    char small[16];
    char large[128];
    snprintf(small, sizeof(large), "x = %d", x);  // ERROR: 128 > 16
}

// =============================================================================
// SECTION 8: Pointer arithmetic overflow with strcpy
// =============================================================================

// Variant 8.1: strcpy to offset position overflow
void test_strcpy_offset_overflow() {
    char buf[20] = "prefix: ";  // 8 chars, 12 remaining
    strcpy(buf + 8, "this is too long");  // ERROR: 17 > 12
}

// Variant 8.2: strncpy to offset with wrong size
void test_strncpy_offset_wrong(const char* src) {
    char buf[50];
    memset(buf, 0, sizeof(buf));
    strncpy(buf + 40, src, 20);  // ERROR: 20 > 10 remaining
}

// =============================================================================
// SECTION 9: Struct member overflow
// =============================================================================

// Variant 9.1: strcpy to struct member too long
void test_struct_overflow(const char* name) {
    struct S { char name[16]; int id; };
    S s;
    strcpy(s.name, name);  // ERROR: name length unguarded
}

// Variant 9.2: struct member known overflow
void test_struct_literal_overflow() {
    struct S { char buf[8]; };
    S s;
    strcpy(s.buf, "this is way too long");  // ERROR: 21 > 8
}

// =============================================================================
// SECTION 10: Wrong guard condition
// =============================================================================

// Variant 10.1: guard checks wrong size
void test_wrong_size_guard(const char* src) {
    char dest[50];
    char other[100];
    if (strlen(src) < sizeof(other)) {  // Wrong buffer!
        strcpy(dest, src);  // ERROR: guarded against wrong buffer
    }
}

// Variant 10.2: guard off-by-one
void test_guard_off_by_one(const char* src) {
    char buf[64];
    if (strlen(src) <= sizeof(buf)) {  // Should be <, not <=
        strcpy(buf, src);  // ERROR: allows strlen == 64, needs 65 with null
    }
}

// Variant 10.3: guard uses wrong variable
void test_wrong_var_guard(const char* src, const char* other) {
    char buf[100];
    if (strlen(other) < sizeof(buf)) {  // Wrong! Checks 'other'
        strcpy(buf, src);  // ERROR: src not guarded
    }
}

// =============================================================================
// SECTION 11: fgets issues
// =============================================================================

// Variant 11.1: fgets n > buffer
#include <cstdio>
void test_fgets_overflow(FILE* f) {
    char buf[50];
    fgets(buf, 100, f);  // ERROR: 100 > 50
}

// =============================================================================
// SECTION 12: gets (deprecated, always error)
// =============================================================================

// Note: gets is removed in C11, but some old code still uses it
// #include <cstdio>
// void test_gets_always_bad() {
//     char buf[100];
//     gets(buf);  // ERROR: gets() is always unsafe, deprecated
// }

// =============================================================================
// SECTION 13: memcpy with strlen without +1
// =============================================================================

// Variant 13.1: memcpy forgets null terminator
void test_memcpy_no_null(const char* src) {
    char buf[100];
    size_t len = strlen(src);
    if (len < sizeof(buf)) {
        memcpy(buf, src, len);  // WARN: doesn't copy null terminator
        // buf is not null-terminated!
    }
}

int main() {
    return 0;
}
