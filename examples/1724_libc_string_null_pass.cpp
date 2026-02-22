// 1724_libc_string_null_pass.cpp
// Tests: Safe usage of libc string functions with proper null checks
// DTT should accept all of these

#include <cstring>
#include <cstdlib>

char* get_nullable_string();  // May return null

// =============================================================================
// SECTION 1: strlen - safe patterns
// =============================================================================

// Variant 1.1: strlen on string literal (always non-null)
void test_strlen_literal() {
    size_t len = strlen("hello");  // OK: literal is never null
    (void)len;
}

// Variant 1.2: strlen after null check
void test_strlen_guarded() {
    char* s = get_nullable_string();
    if (s) {
        size_t len = strlen(s);  // OK: null-checked
        (void)len;
    }
}

// Variant 1.3: strlen after early return
void test_strlen_early_return() {
    char* s = get_nullable_string();
    if (!s) return;
    size_t len = strlen(s);  // OK: guarded by early return
    (void)len;
}

// =============================================================================
// SECTION 2: strcmp / strncmp - safe patterns
// =============================================================================

// Variant 2.1: strcmp on two literals
void test_strcmp_literals() {
    int r = strcmp("abc", "def");  // OK: literals never null
    (void)r;
}

// Variant 2.2: strcmp after null checks
void test_strcmp_guarded() {
    char* a = get_nullable_string();
    char* b = get_nullable_string();
    if (a && b) {
        int r = strcmp(a, b);  // OK: both null-checked
        (void)r;
    }
}

// Variant 2.3: strncmp after null check
void test_strncmp_guarded() {
    char* s = get_nullable_string();
    if (s) {
        int r = strncmp(s, "test", 4);  // OK: guarded
        (void)r;
    }
}

// =============================================================================
// SECTION 3: strchr / strrchr / strstr - safe patterns
// =============================================================================

// Variant 3.1: strchr on literal
void test_strchr_literal() {
    const char* p = strchr("hello world", ' ');  // OK: literal
    (void)p;
}

// Variant 3.2: strrchr after null check
void test_strrchr_guarded() {
    char* s = get_nullable_string();
    if (s) {
        char* p = strrchr(s, '/');  // OK: guarded
        (void)p;
    }
}

// Variant 3.3: strstr after null check
void test_strstr_guarded() {
    char* s = get_nullable_string();
    if (s) {
        char* p = strstr(s, "needle");  // OK: guarded
        (void)p;
    }
}

// =============================================================================
// SECTION 4: strspn / strcspn / strpbrk - safe patterns
// =============================================================================

// Variant 4.1: strspn on local array
void test_strspn_local() {
    char buf[] = "abc123";
    size_t n = strspn(buf, "abc");  // OK: local array, never null
    (void)n;
}

// Variant 4.2: strcspn after null check
void test_strcspn_guarded() {
    char* s = get_nullable_string();
    if (s) {
        size_t n = strcspn(s, "xyz");  // OK: guarded
        (void)n;
    }
}

// Variant 4.3: strpbrk after early return
void test_strpbrk_guarded() {
    char* s = get_nullable_string();
    if (!s) return;
    char* p = strpbrk(s, "aeiou");  // OK: guarded by early return
    (void)p;
}

int main() { return 0; }
