// 1724_libc_string_null_fail.cpp
// Tests: Missing null-check annotations for common libc string functions
// These pass a possibly-null pointer without checking - DTT should catch them

#include <cstring>
#include <cstdlib>

char* get_nullable_string();  // May return null

// =============================================================================
// SECTION 1: strlen - null pointer
// =============================================================================

// Variant 1.1: strlen on possibly-null pointer
void test_strlen_null() {
    char* s = get_nullable_string();
    size_t len = strlen(s);  // ERROR: s may be null
    (void)len;
}

// =============================================================================
// SECTION 2: strcmp / strncmp - null pointer
// =============================================================================

// Variant 2.1: strcmp with null first arg
void test_strcmp_null_first() {
    char* s = get_nullable_string();
    int r = strcmp(s, "hello");  // ERROR: s may be null
    (void)r;
}

// Variant 2.2: strcmp with null second arg
void test_strcmp_null_second() {
    char* s = get_nullable_string();
    int r = strcmp("hello", s);  // ERROR: s may be null
    (void)r;
}

// Variant 2.3: strncmp with null first arg
void test_strncmp_null() {
    char* s = get_nullable_string();
    int r = strncmp(s, "test", 4);  // ERROR: s may be null
    (void)r;
}

// =============================================================================
// SECTION 3: strchr / strrchr / strstr - null pointer
// =============================================================================

// Variant 3.1: strchr on null
void test_strchr_null() {
    char* s = get_nullable_string();
    char* p = strchr(s, 'x');  // ERROR: s may be null
    (void)p;
}

// Variant 3.2: strrchr on null
void test_strrchr_null() {
    char* s = get_nullable_string();
    char* p = strrchr(s, 'x');  // ERROR: s may be null
    (void)p;
}

// Variant 3.3: strstr with null haystack
void test_strstr_null() {
    char* s = get_nullable_string();
    char* p = strstr(s, "needle");  // ERROR: s may be null
    (void)p;
}

// =============================================================================
// SECTION 4: strspn / strcspn / strpbrk - null pointer
// =============================================================================

// Variant 4.1: strspn with null
void test_strspn_null() {
    char* s = get_nullable_string();
    size_t n = strspn(s, "abc");  // ERROR: s may be null
    (void)n;
}

// Variant 4.2: strcspn with null
void test_strcspn_null() {
    char* s = get_nullable_string();
    size_t n = strcspn(s, "xyz");  // ERROR: s may be null
    (void)n;
}

// Variant 4.3: strpbrk with null
void test_strpbrk_null() {
    char* s = get_nullable_string();
    char* p = strpbrk(s, "aeiou");  // ERROR: s may be null
    (void)p;
}

int main() { return 0; }
