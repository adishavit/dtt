// 1725_libc_conversion_null_fail.cpp
// Tests: Missing null-check annotations for string-to-number conversion functions
// These pass a possibly-null pointer without checking - DTT should catch them

#include <cstdlib>

char* get_nullable_string();  // May return null

// =============================================================================
// SECTION 1: atoi / atol / atof - null pointer
// =============================================================================

// Variant 1.1: atoi on null
void test_atoi_null() {
    char* s = get_nullable_string();
    int val = atoi(s);  // ERROR: s may be null
    (void)val;
}

// Variant 1.2: atol on null
void test_atol_null() {
    char* s = get_nullable_string();
    long val = atol(s);  // ERROR: s may be null
    (void)val;
}

// Variant 1.3: atof on null
void test_atof_null() {
    char* s = get_nullable_string();
    double val = atof(s);  // ERROR: s may be null
    (void)val;
}

// =============================================================================
// SECTION 2: strtol / strtoul family - null pointer
// =============================================================================

// Variant 2.1: strtol on null
void test_strtol_null() {
    char* s = get_nullable_string();
    long val = strtol(s, nullptr, 10);  // ERROR: s may be null
    (void)val;
}

// Variant 2.2: strtoul on null
void test_strtoul_null() {
    char* s = get_nullable_string();
    unsigned long val = strtoul(s, nullptr, 10);  // ERROR: s may be null
    (void)val;
}

// Variant 2.3: strtoll on null
void test_strtoll_null() {
    char* s = get_nullable_string();
    long long val = strtoll(s, nullptr, 10);  // ERROR: s may be null
    (void)val;
}

// Variant 2.4: strtoull on null
void test_strtoull_null() {
    char* s = get_nullable_string();
    unsigned long long val = strtoull(s, nullptr, 10);  // ERROR: s may be null
    (void)val;
}

// =============================================================================
// SECTION 3: strtof / strtod - null pointer
// =============================================================================

// Variant 3.1: strtof on null
void test_strtof_null() {
    char* s = get_nullable_string();
    float val = strtof(s, nullptr);  // ERROR: s may be null
    (void)val;
}

// Variant 3.2: strtod on null
void test_strtod_null() {
    char* s = get_nullable_string();
    double val = strtod(s, nullptr);  // ERROR: s may be null
    (void)val;
}

int main() { return 0; }
