// WHAT IS TESTED:
// Guard-based bounds - actual OOB that DTT should catch.
// These are cases where the guard does NOT protect the access.
//
// Expected behavior: FAIL (DTT catches OOB access)
//
// ============================================================================

#include <cstdint>

static const unsigned table8[8] = {0, 1, 2, 1, 0, 4, 4, 4};

// -----------------------------------------------------------------------------
// VARIANT 1: Access outside guard
// -----------------------------------------------------------------------------
void variant1_outside_guard(unsigned offset) {
    if (offset < 8) {
        // Safe here
    }
    // Guard no longer applies
    unsigned val = table8[offset];  // FAIL: offset could be >= 8  dtt-error {{out of bounds|cannot verify}}
    (void)val;
}

// -----------------------------------------------------------------------------
// VARIANT 2: Wrong guard size
// -----------------------------------------------------------------------------
void variant2_wrong_guard_size(unsigned offset) {
    if (offset < 10) {  // Guard too loose
        unsigned val = table8[offset];  // FAIL: offset could be 8 or 9  dtt-error {{out of bounds|cannot verify}}
        (void)val;
    }
}

// -----------------------------------------------------------------------------
// VARIANT 3: Guard on wrong variable
// -----------------------------------------------------------------------------
void variant3_wrong_variable(unsigned a, unsigned b) {
    if (a < 8) {
        unsigned val = table8[b];  // FAIL: b not guarded  dtt-error {{out of bounds|cannot verify}}
        (void)val;
    }
}

// -----------------------------------------------------------------------------
// VARIANT 4: No guard at all
// -----------------------------------------------------------------------------
void variant4_no_guard(unsigned offset) {
    unsigned val = table8[offset];  // FAIL: no guard  dtt-error {{out of bounds|cannot verify}}
    (void)val;
}

// -----------------------------------------------------------------------------
// VARIANT 5: Constant OOB
// -----------------------------------------------------------------------------
void variant5_constant_oob() {
    unsigned val = table8[8];  // FAIL: constant index 8 >= size 8  dtt-error {{out of bounds}}
    (void)val;
}

// -----------------------------------------------------------------------------
// VARIANT 6: Guard in wrong direction
// -----------------------------------------------------------------------------
void variant6_wrong_direction(unsigned offset) {
    if (offset > 8) {
        return;  // This proves offset <= 8, not offset < 8
    }
    unsigned val = table8[offset];  // FAIL: offset could be exactly 8  dtt-error {{out of bounds|cannot verify}}
    (void)val;
}

// -----------------------------------------------------------------------------
// VARIANT 7: Modified after guard
// -----------------------------------------------------------------------------
void variant7_modified_after_guard(unsigned offset) {
    if (offset < 8) {
        offset++;  // Now offset could be 8
        unsigned val = table8[offset];  // KNOWN GAP (not asserted): offset modified
        (void)val;
    }
}

int main() {
    variant1_outside_guard(5);
    variant2_wrong_guard_size(5);
    variant3_wrong_variable(5, 10);
    variant4_no_guard(5);
    variant5_constant_oob();
    variant6_wrong_direction(5);
    variant7_modified_after_guard(5);
    return 0;
}
