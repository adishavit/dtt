// 714_uaf_clib_args_fail.cpp
// Test: C library functions should detect use-after-free on pointer arguments
// Expected: DTT errors for passing freed pointers to string/memory functions

#include <cstdlib>
#include <cstring>
#include <cstdio>

// Variant 1: strcpy with freed destination
void test_strcpy_freed_dst() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    free(buf);
    strcpy(buf, "Hello");  // ERROR: use-after-free (dst)
}

// Variant 2: strcpy with freed source
void test_strcpy_freed_src() {
    char* src = (char*)malloc(100);
    char dst[100];
    if (!src) return;
    strcpy(src, "Hello");
    free(src);
    strcpy(dst, src);  // ERROR: use-after-free (src)
}

// Variant 3: strcat with freed buffer
void test_strcat_freed() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    strcpy(buf, "Hello");
    free(buf);
    strcat(buf, " World");  // ERROR: use-after-free
}

// Variant 4: strlen on freed string
void test_strlen_freed() {
    char* str = (char*)malloc(100);
    if (!str) return;
    strcpy(str, "Hello");
    free(str);
    size_t len = strlen(str);  // ERROR: use-after-free
    (void)len;
}

// Variant 5: strcmp with freed string
void test_strcmp_freed() {
    char* s1 = (char*)malloc(10);
    if (!s1) return;
    strcpy(s1, "test");
    free(s1);
    int cmp = strcmp(s1, "test");  // ERROR: use-after-free
    (void)cmp;
}

// Variant 6: memcpy from freed source
void test_memcpy_freed_src() {
    char* src = (char*)malloc(100);
    char dst[100];
    if (!src) return;
    memcpy(src, "Hello", 6);
    free(src);
    memcpy(dst, src, 6);  // ERROR: use-after-free (src)
}

// Variant 7: memcpy to freed destination
void test_memcpy_freed_dst() {
    char* dst = (char*)malloc(100);
    if (!dst) return;
    free(dst);
    memcpy(dst, "Hello", 6);  // ERROR: use-after-free (dst)
}

// Variant 8: memset on freed buffer
void test_memset_freed() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    free(buf);
    memset(buf, 0, 100);  // ERROR: use-after-free
}

// Variant 9: sprintf to freed buffer
void test_sprintf_freed() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    free(buf);
    sprintf(buf, "Value: %d", 42);  // ERROR: use-after-free
}

// Variant 10: snprintf to freed buffer
void test_snprintf_freed() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    free(buf);
    snprintf(buf, 100, "Value: %d", 42);  // ERROR: use-after-free
}

// Variant 11: memmove with freed buffers
void test_memmove_freed() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    memcpy(buf, "Hello World", 12);
    free(buf);
    memmove(buf + 6, buf, 5);  // ERROR: use-after-free (both args)
}

// Variant 12: memcmp with freed buffer
void test_memcmp_freed() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    memcpy(buf, "Hello", 6);
    free(buf);
    int cmp = memcmp(buf, "Hello", 5);  // ERROR: use-after-free
    (void)cmp;
}

// Variant 13: strncpy with freed source
void test_strncpy_freed() {
    char* src = (char*)malloc(100);
    char dst[100];
    if (!src) return;
    strcpy(src, "Hello");
    free(src);
    strncpy(dst, src, 50);  // ERROR: use-after-free
}

// Variant 14: strncat with freed buffer
void test_strncat_freed() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    strcpy(buf, "Hello");
    free(buf);
    strncat(buf, " World", 6);  // ERROR: use-after-free
}

// Variant 15: strncmp with freed string
void test_strncmp_freed() {
    char* s1 = (char*)malloc(10);
    if (!s1) return;
    strcpy(s1, "test");
    free(s1);
    int cmp = strncmp(s1, "test", 4);  // ERROR: use-after-free
    (void)cmp;
}

int main() {
    return 0;
}
