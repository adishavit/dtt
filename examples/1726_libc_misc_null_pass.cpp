// 1726_libc_misc_null_pass.cpp
// Tests: Safe usage of puts, getenv, and time functions with proper null checks
// DTT should accept all of these

#include <cstdio>
#include <cstdlib>
#include <ctime>

char* get_nullable_string();  // May return null

// =============================================================================
// SECTION 1: puts - safe patterns
// =============================================================================

// Variant 1.1: puts on literal
void test_puts_literal() {
    puts("hello world");  // OK: literal never null
}

// Variant 1.2: puts after null check
void test_puts_guarded() {
    char* s = get_nullable_string();
    if (s) {
        puts(s);  // OK: guarded
    }
}

// =============================================================================
// SECTION 2: getenv - safe patterns
// =============================================================================

// Variant 2.1: getenv with literal name
void test_getenv_literal() {
    char* val = getenv("PATH");  // OK: literal name never null
    (void)val;
}

// Variant 2.2: getenv after null check
void test_getenv_guarded() {
    char* name = get_nullable_string();
    if (name) {
        char* val = getenv(name);  // OK: guarded
        (void)val;
    }
}

// =============================================================================
// SECTION 3: time functions - safe patterns
// =============================================================================

// Variant 3.1: localtime with stack variable
void test_localtime_stack() {
    time_t t = time(nullptr);
    struct tm* result = localtime(&t);  // OK: &t is never null
    (void)result;
}

// Variant 3.2: gmtime with stack variable
void test_gmtime_stack() {
    time_t t = time(nullptr);
    struct tm* result = gmtime(&t);  // OK: &t is never null
    (void)result;
}

// Variant 3.3: ctime with stack variable
void test_ctime_stack() {
    time_t t = time(nullptr);
    char* result = ctime(&t);  // OK: &t is never null
    (void)result;
}

// Variant 3.4: mktime with stack variable
void test_mktime_stack() {
    struct tm t = {};
    time_t result = mktime(&t);  // OK: &t is never null
    (void)result;
}

// Variant 3.5: asctime with stack variable
void test_asctime_stack() {
    struct tm t = {};
    char* result = asctime(&t);  // OK: &t is never null
    (void)result;
}

// Variant 3.6: strftime with all valid args
void test_strftime_stack() {
    char buf[64];
    time_t now = time(nullptr);
    struct tm t = {};
    strftime(buf, sizeof(buf), "%Y-%m-%d", &t);  // OK: both non-null
}

int main() { return 0; }
