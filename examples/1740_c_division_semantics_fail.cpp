// 1740: C integer division and remainder truncate toward zero
//
// Each index below exceeds the upper bound under C semantics (x / y rounds
// toward zero, x % y has the sign of x), but is in bounds if / and % round
// toward negative infinity. The range guards pin x to one value, so the
// index is decided by the solver, not by constant folding. Negative indices
// are not used: see docs/HEURISTICS.md #2.

// Variant 1: -1 / 2 == 0, so the index is 4.
void variant1_div_negative(int x) {
    int a[4];
    if (x < 0 && x > -2)
        a[4 + x / 2] = 1;  // dtt-error {{out of bounds}}
}

// Variant 2: -1 % 2 == -1, so the index is 4.
void variant2_rem_negative(int x) {
    int a[4];
    if (x < 0 && x > -2)
        a[3 - x % 2] = 1;  // dtt-error {{out of bounds}}
}

// Variant 3: -7 / -2 == 3, so the index is 4.
void variant3_div_both_negative(int x) {
    int a[4];
    if (x < -6 && x > -8)
        a[7 - x / -2] = 1;  // dtt-error {{out of bounds}}
}

// Variant 4: -7 % -2 == -1, so the index is 4.
void variant4_rem_both_negative(int x) {
    int a[4];
    if (x < -6 && x > -8)
        a[3 - x % -2] = 1;  // dtt-error {{out of bounds}}
}

int main() { return 0; }
