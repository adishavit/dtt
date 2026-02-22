// 1725_libc_conversion_null_pass.cpp
// Tests: Safe usage of string-to-number conversion with proper null checks
// DTT should accept all of these

#include <cstdlib>

char* get_nullable_string();  // May return null

// =============================================================================
// SECTION 1: atoi / atol / atof - safe patterns
// =============================================================================

// Variant 1.1: atoi on literal
void test_atoi_literal() {
    int val = atoi("42");  // OK: literal never null
    (void)val;
}

// Variant 1.2: atol after null check
void test_atol_guarded() {
    char* s = get_nullable_string();
    if (s) {
        long val = atol(s);  // OK: guarded
        (void)val;
    }
}

// Variant 1.3: atof after early return
void test_atof_early_return() {
    char* s = get_nullable_string();
    if (!s) return;
    double val = atof(s);  // OK: guarded by early return
    (void)val;
}

// =============================================================================
// SECTION 2: strtol / strtoul family - safe patterns
// =============================================================================

// Variant 2.1: strtol on literal
void test_strtol_literal() {
    long val = strtol("100", nullptr, 10);  // OK: literal
    (void)val;
}

// Variant 2.2: strtoul after null check
void test_strtoul_guarded() {
    char* s = get_nullable_string();
    if (s) {
        unsigned long val = strtoul(s, nullptr, 10);  // OK: guarded
        (void)val;
    }
}

// Variant 2.3: strtoll after early return
void test_strtoll_early_return() {
    char* s = get_nullable_string();
    if (!s) return;
    long long val = strtoll(s, nullptr, 10);  // OK: guarded
    (void)val;
}

// Variant 2.4: strtoull after null check
void test_strtoull_guarded() {
    char* s = get_nullable_string();
    if (s) {
        unsigned long long val = strtoull(s, nullptr, 10);  // OK: guarded
        (void)val;
    }
}

// =============================================================================
// SECTION 3: strtof / strtod - safe patterns
// =============================================================================

// Variant 3.1: strtof after null check
void test_strtof_guarded() {
    char* s = get_nullable_string();
    if (s) {
        float val = strtof(s, nullptr);  // OK: guarded
        (void)val;
    }
}

// Variant 3.2: strtod on literal
void test_strtod_literal() {
    double val = strtod("3.14", nullptr);  // OK: literal
    (void)val;
}

// Variant 3.3: strtold after null check
void test_strtold_guarded() {
    char* s = get_nullable_string();
    if (s) {
        long double val = strtold(s, nullptr);  // OK: guarded
        (void)val;
    }
}

int main() { return 0; }
