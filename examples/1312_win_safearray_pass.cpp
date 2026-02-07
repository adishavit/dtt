// 1312_win_safearray_pass.cpp
// Tests: Safe SafeArray usage that should compile without DTT errors

#include "win_api.h"

// =============================================================================
// SECTION 1: SafeArrayCreate + SafeArrayDestroy
// =============================================================================

// Variant 1.1: Create + null check + Destroy
void test_safearray_create_destroy() {
    SAFEARRAYBOUND bound = {10, 0};
    SAFEARRAY* psa = SafeArrayCreate(0, 1, &bound);
    if (psa) {
        SafeArrayDestroy(psa);
    }
}

// Variant 1.2: Early return on null
void test_safearray_early_return() {
    SAFEARRAYBOUND bound = {5, 0};
    SAFEARRAY* psa = SafeArrayCreate(0, 1, &bound);
    if (!psa) return;
    SafeArrayDestroy(psa);
}

// =============================================================================
// SECTION 2: SafeArrayCreateVector + SafeArrayDestroy
// =============================================================================

// Variant 2.1: CreateVector + null check + Destroy
void test_safearrayvector_create_destroy() {
    SAFEARRAY* psa = SafeArrayCreateVector(0, 0, 10, 4);
    if (psa) {
        SafeArrayDestroy(psa);
    }
}

// Variant 2.2: CreateVector early return
void test_safearrayvector_early_return() {
    SAFEARRAY* psa = SafeArrayCreateVector(0, 0, 20, 8);
    if (!psa) return;
    SafeArrayDestroy(psa);
}

int main() { return 0; }
