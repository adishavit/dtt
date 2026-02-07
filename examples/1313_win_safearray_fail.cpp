// 1313_win_safearray_fail.cpp
// Tests: Unsafe SafeArray usage that DTT should catch

#include "win_api.h"

// =============================================================================
// SECTION 1: SafeArray leak (scope-exit violation)
// =============================================================================

// Variant 1.1: SafeArrayCreate without SafeArrayDestroy
void test_safearray_leak() {
    SAFEARRAYBOUND bound = {10, 0};
    SAFEARRAY* psa = SafeArrayCreate(0, 1, &bound);
    // ERROR: @safearray == 1 at scope exit, need @safearray == 0
}

// Variant 1.2: SafeArrayCreateVector without destroy
void test_safearrayvector_leak() {
    SAFEARRAY* psa = SafeArrayCreateVector(0, 0, 10, 4);
    // ERROR: @safearray == 1 at scope exit, need @safearray == 0
}

// =============================================================================
// SECTION 2: Double SafeArrayDestroy
// =============================================================================

// Variant 2.1: SafeArrayDestroy twice
void test_safearray_double_destroy() {
    SAFEARRAYBOUND bound = {10, 0};
    SAFEARRAY* psa = SafeArrayCreate(0, 1, &bound);
    if (psa) {
        SafeArrayDestroy(psa);
        SafeArrayDestroy(psa);  // ERROR: SAFEARRAY already destroyed
    }
}

int main() { return 0; }
