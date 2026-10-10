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
    puts(s);  // ERROR: s may be null  dtt-error {{_nonnull != 0}}
}

// =============================================================================
// SECTION 2: getenv - null pointer (for the parameter)
// =============================================================================

// Variant 2.1: getenv with null name
void test_getenv_null() {
    char* name = get_nullable_string();
    char* val = getenv(name);  // ERROR: name may be null  dtt-error {{_nonnull != 0}}
    (void)val;
}

// =============================================================================
// SECTION 3: time functions - null pointer
// =============================================================================

// Variant 3.1: localtime on null
void test_localtime_null() {
    time_t* t = nullptr;
    struct tm* result = localtime(t);  // ERROR: t is null  dtt-error {{_nonnull != 0}}
    (void)result;
}

// Variant 3.2: gmtime on null
void test_gmtime_null() {
    time_t* t = nullptr;
    struct tm* result = gmtime(t);  // ERROR: t is null  dtt-error {{_nonnull != 0}}
    (void)result;
}

// Variant 3.3: ctime on null
void test_ctime_null() {
    time_t* t = nullptr;
    char* result = ctime(t);  // ERROR: t is null  dtt-error {{_nonnull != 0}}
    (void)result;
}

// Variant 3.4: mktime on null
void test_mktime_null() {
    struct tm* t = nullptr;
    time_t result = mktime(t);  // ERROR: t is null  dtt-error {{_nonnull != 0}}
    (void)result;
}

// Variant 3.5: asctime on null
void test_asctime_null() {
    struct tm* t = nullptr;
    char* result = asctime(t);  // ERROR: t is null  dtt-error {{_nonnull != 0}}
    (void)result;
}

// Variant 3.6: strftime with null format
void test_strftime_null_format() {
    char buf[64];
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    if (t) {
        const char* fmt = nullptr;
        strftime(buf, sizeof(buf), fmt, t);  // ERROR: fmt is null  dtt-error {{_nonnull != 0}}
    }
}

// Variant 3.7: strftime with null tm pointer
void test_strftime_null_tm() {
    char buf[64];
    struct tm* t = nullptr;
    strftime(buf, sizeof(buf), "%Y-%m-%d", t);  // ERROR: t is null  dtt-error {{_nonnull != 0}}
}

int main() { return 0; }
