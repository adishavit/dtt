// 1750: Solver soundness - unknown values stay unknown
//
// Every index below may be out of bounds, so each access must be reported
// ("cannot verify" is an error in enforce mode). Before the fix, unknown
// values shared one solver symbol: a fact like `t == g() + 1` became
// `t == t + 1`, the facts contradicted each other, and every later check
// counted as proven.
#include <vector>

int g();
int h();
int a[10];

// Variant 1: an unknown sum in the environment.
int variant1_unknown_sum(int i) {
    int t = g() + 1;
    (void)t;
    return a[i];  // dtt-error {{out of bounds|could not be verified}}
}

// Variant 2: two unknowns are not equal.
int variant2_unknown_difference() {
    return a[g() - h()];  // dtt-error {{out of bounds|could not be verified}}
}

// Variant 3: two bitwise results are not equal.
int variant3_bitwise_difference(int x, int y) {
    return a[(x & 255) - (y & 255)];  // dtt-error {{out of bounds|could not be verified}}
}

// Variant 4: a variable named like a solver placeholder.
int variant4_placeholder_name() {
    int idx = 3;
    return a[idx + g()];  // dtt-error {{out of bounds|could not be verified}}
}

// Variant 5: an unknown sum before a container access.
int variant5_vector(int i) {
    std::vector<int> v(10);
    int t = g() - 1;
    (void)t;
    return v[i];  // dtt-error {{cannot verify}}
}

// Variant 6: a violation that holds on every path stays a proven error.
int variant6_proven(int i) {
    if (i > 20)
        return a[i];  // dtt-error {{out of bounds}}
    return 0;
}

// Variant 7: C remainder keeps the dividend's sign: x = -9 reads a[18].
int variant7_negative_remainder(int x) {
    return a[9 - x % 10];  // dtt-error {{out of bounds|could not be verified}}
}

// Variant 8: an index the facts force negative is not excused by the
// signed-index convention (docs/HEURISTICS.md #2).
int variant8_forced_negative(int x) {
    if (x < 0)
        return a[x];  // dtt-error {{out of bounds}}
    return 0;
}

int main() { return 0; }
