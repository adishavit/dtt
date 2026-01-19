// 878_strcat_pass.cpp
// Tests: Safe strcat/strncat usage that should NOT produce errors

#include <cstring>
#include <cstddef>

// =============================================================================
// SECTION 1: strcat with string literals (dest large enough)
// =============================================================================

// Variant 1.1: strcat with small literal into large buffer
void test_strcat_small_literal() {
    char dest[100] = "";  // Empty string, strlen(dest) = 0
    strcat(dest, "hello");  // OK: 0 + 5 + 1 = 6 <= 100
}

// Variant 1.2: strcat multiple small literals
void test_strcat_multiple() {
    char dest[100] = "";
    strcat(dest, "hello");  // OK: 6 <= 100
    strcat(dest, " ");      // OK: 6 + 1 + 1 = 8 <= 100 (but we can't track this yet)
}

// Variant 1.3: strcat with sizeof room
void test_strcat_sizeof() {
    char dest[20] = "";
    strcat(dest, "test");  // OK: 5 <= 20
}

// =============================================================================
// SECTION 2: strncat safe patterns
// =============================================================================

// Variant 2.1: strncat with n smaller than buffer
void test_strncat_small_n() {
    char dest[100] = "";
    strncat(dest, "hello world", 5);  // OK: 0 + 5 + 1 = 6 <= 100
}

// Variant 2.2: strncat with sizeof-based calculation
void test_strncat_sizeof() {
    char dest[50] = "";
    strncat(dest, "source", sizeof(dest) - 1);  // OK: 49 + 1 <= 50
}

// Variant 2.3: strncat with explicit safe size
void test_strncat_explicit() {
    char dest[32] = "";
    strncat(dest, "data", 10);  // OK: 0 + 10 + 1 = 11 <= 32
}

int main() { return 0; }
