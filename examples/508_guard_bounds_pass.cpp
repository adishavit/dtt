// WHAT IS TESTED:
// Guard-based bounds inference: if (idx < N) proves arr[idx] is safe.
// This is the lz4 pattern where a comparison guard establishes bounds.
//
// Expected behavior: PASS (DTT recognizes guard proves safety)
//
// ORIGIN: Found in lz4.c:508 - inc32table[8] accessed with if (offset < 8) guard
//
// ============================================================================

#include <cstdint>

// -----------------------------------------------------------------------------
// VARIANT 1: Simple less-than guard (the lz4 case)
// -----------------------------------------------------------------------------
static const unsigned table8[8] = {0, 1, 2, 1, 0, 4, 4, 4};

void variant1_simple_lt_guard(unsigned offset) {
    if (offset < 8) {
        unsigned val = table8[offset];  // Safe: guard proves offset < 8
        (void)val;
    }
}

// -----------------------------------------------------------------------------
// VARIANT 2: Less-than-or-equal guard
// -----------------------------------------------------------------------------
static const int table9[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};

void variant2_lte_guard(int idx) {
    if (idx >= 0 && idx <= 8) {
        int val = table9[idx];  // Safe: guard proves 0 <= idx <= 8
        (void)val;
    }
}

// -----------------------------------------------------------------------------
// VARIANT 3: Guard in else branch (negated condition)
// -----------------------------------------------------------------------------
void variant3_else_branch(unsigned offset) {
    if (offset >= 8) {
        return;
    }
    // Here offset < 8 is proven
    unsigned val = table8[offset];  // Safe
    (void)val;
}

// -----------------------------------------------------------------------------
// VARIANT 4: Multiple accesses after single guard
// -----------------------------------------------------------------------------
static const int dec64[8] = {0, 0, 0, -1, -4, 1, 2, 3};

void variant4_multiple_accesses(unsigned offset) {
    if (offset < 8) {
        unsigned a = table8[offset];   // Safe
        int b = dec64[offset];         // Safe
        (void)a; (void)b;
    }
}

// -----------------------------------------------------------------------------
// VARIANT 5: Guard with different comparison styles
// -----------------------------------------------------------------------------
void variant5_comparison_styles(unsigned i) {
    // All equivalent guards
    if (i < 8) {
        (void)table8[i];
    }
    if (8 > i) {
        (void)table8[i];
    }
    if (!(i >= 8)) {
        (void)table8[i];
    }
}

// -----------------------------------------------------------------------------
// VARIANT 6: Nested guard strengthens bounds
// -----------------------------------------------------------------------------
static const int table4[4] = {1, 2, 3, 4};

void variant6_nested_guards(unsigned i) {
    if (i < 8) {
        (void)table8[i];  // Safe: i < 8
        if (i < 4) {
            (void)table4[i];  // Safe: i < 4
        }
    }
}

// -----------------------------------------------------------------------------
// VARIANT 7: Guard in loop condition
// -----------------------------------------------------------------------------
void variant7_loop_guard() {
    for (unsigned i = 0; i < 8; ++i) {
        (void)table8[i];  // Safe: loop bounds prove i < 8
    }
}

// -----------------------------------------------------------------------------
// VARIANT 8: Guard with early return pattern
// -----------------------------------------------------------------------------
void variant8_early_return(unsigned offset) {
    if (offset >= 8) return;
    if (offset < 0) return;  // Can't happen for unsigned, but pattern exists

    (void)table8[offset];  // Safe: offset in [0, 7]
}

// -----------------------------------------------------------------------------
// VARIANT 9: Size from sizeof
// -----------------------------------------------------------------------------
void variant9_sizeof_bound(unsigned i) {
    if (i < sizeof(table8) / sizeof(table8[0])) {
        (void)table8[i];  // Safe
    }
}

// -----------------------------------------------------------------------------
// VARIANT 10: Local array with guard
// -----------------------------------------------------------------------------
void variant10_local_array(unsigned idx) {
    int local[16] = {};

    if (idx < 16) {
        local[idx] = 42;  // Safe
    }
}

// -----------------------------------------------------------------------------
// VARIANT 11: Constant index after variable guard (should still work)
// -----------------------------------------------------------------------------
void variant11_constant_after_guard(unsigned offset) {
    if (offset < 8) {
        (void)table8[0];  // Safe: constant index
        (void)table8[7];  // Safe: constant index
    }
}

// -----------------------------------------------------------------------------
// VARIANT 12: Pointer arithmetic with guard
// -----------------------------------------------------------------------------
void variant12_pointer_guard(unsigned offset) {
    const unsigned* p = table8;
    if (offset < 8) {
        unsigned val = p[offset];  // Safe
        (void)val;
    }
}

int main() {
    variant1_simple_lt_guard(3);
    variant2_lte_guard(5);
    variant3_else_branch(2);
    variant4_multiple_accesses(4);
    variant5_comparison_styles(1);
    variant6_nested_guards(2);
    variant7_loop_guard();
    variant8_early_return(6);
    variant9_sizeof_bound(4);
    variant10_local_array(10);
    variant11_constant_after_guard(5);
    variant12_pointer_guard(7);
    return 0;
}
