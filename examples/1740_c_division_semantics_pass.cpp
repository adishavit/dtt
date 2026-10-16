// 1740: C integer division and remainder truncate toward zero
//
// Each index below is in bounds under C semantics. Runs in enforce mode, so
// an index the solver cannot prove in bounds fails the test.

// Variant 1: -1 / 2 == 0, so the index is 3.
void variant1_div_negative(int x) {
    int a[4];
    if (x < 0 && x > -2)
        a[3 - x / 2] = 1;
}

// Variant 2: -1 % 2 == -1, so the index is 3.
void variant2_rem_negative(int x) {
    int a[4];
    if (x < 0 && x > -2)
        a[x % 2 + 4] = 1;
}

// Variant 3: -7 / -2 == 3.
void variant3_div_both_negative(int x) {
    int a[4];
    if (x < -6 && x > -8)
        a[x / -2] = 1;
}

// Variant 4: non-negative operands are unaffected: 7 / 2 == 3, 7 % 2 == 1.
void variant4_non_negative(int x) {
    int a[4];
    if (x > 6 && x < 8) {
        a[x / 2] = 1;
        a[x % 2] = 1;
    }
}

int main() { return 0; }
