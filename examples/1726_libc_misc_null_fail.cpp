// 1726_libc_misc_null_fail.cpp
// Tests: Missing null-check annotations for puts, getenv, and time functions
// These pass a possibly-null pointer without checking - DTT should catch them

#include <cstdio>
#include <cstdlib>
#include <ctime>

char* get_nullable_string();  // May return null

// =============================================================================
// SECTION 1: puts - null pointer
// =============================================================================

// Variant 1.1: puts on null
void test_puts_null() {
    char* s = get_nullable_string();
    puts(s);  // ERROR: s may be null
}

// =============================================================================
// SECTION 2: getenv - null pointer (for the parameter)
// =============================================================================

// Variant 2.1: getenv with null name
void test_getenv_null() {
    char* name = get_nullable_string();
    char* val = getenv(name);  // ERROR: name may be null
    (void)val;
}

// =============================================================================
// SECTION 3: time functions - null pointer
// =============================================================================

// Variant 3.1: localtime on null
void test_localtime_null() {
    time_t* t = nullptr;
    struct tm* result = localtime(t);  // ERROR: t is null
    (void)result;
}

// Variant 3.2: gmtime on null
void test_gmtime_null() {
    time_t* t = nullptr;
    struct tm* result = gmtime(t);  // ERROR: t is null
    (void)result;
}

// Variant 3.3: ctime on null
void test_ctime_null() {
    time_t* t = nullptr;
    char* result = ctime(t);  // ERROR: t is null
    (void)result;
}

// Variant 3.4: mktime on null
void test_mktime_null() {
    struct tm* t = nullptr;
    time_t result = mktime(t);  // ERROR: t is null
    (void)result;
}

int main() { return 0; }
