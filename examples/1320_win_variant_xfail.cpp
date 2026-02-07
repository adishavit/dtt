// 1320_win_variant_xfail.cpp
// Tests: VARIANT leak that DTT SHOULD catch but can't yet
//
// Known gap: VariantInit sets state on an argument (not return value),
// and FreeFunctionScopeExitReq only works on return values.
// So DTT cannot enforce that VariantClear is called before scope exit.

#include "win_api.h"

// =============================================================================
// SECTION 1: VariantInit without VariantClear
// =============================================================================

// Variant 1.1: VariantInit leak (no VariantClear)
void test_variant_leak() {
    VARIANT v;
    VariantInit(&v);
    // GAP: @variant == 1 at scope exit, should need @variant == 0
    // DTT cannot track this because scope-exit requires only works on return values
}

// Variant 1.2: Conditional VariantClear (one path leaks)
void test_variant_conditional_leak(bool cond) {
    VARIANT v;
    VariantInit(&v);
    if (cond) {
        VariantClear(&v);
    }
    // GAP: if !cond, @variant == 1 at scope exit
}

int main() { return 0; }
