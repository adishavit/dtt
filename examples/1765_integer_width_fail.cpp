// 1765: Integer values have the width and signedness of their type
//
// DTT computed integer values as mathematical integers: an unsigned value
// that wraps below zero (Heartbleed: v[v.size() - 1] on an empty vector),
// a narrowing conversion and an unsigned constant were all taken at their
// mathematical value, so out-of-bounds indices and negative arguments went
// unreported.

#include <cstddef>
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
int a[10];

// Variant 1: an unsigned value in [0, 10] minus 1 wraps when it is 0.
int variant1_unsigned_minus_one(unsigned n) {
    if (n <= 10) return a[n - 1];  // dtt-error {{out of bounds|cannot verify}}
    return 0;
}

// Variant 2: a size_t 0 minus 1 is SIZE_MAX.
int variant2_size_t_zero(std::vector<int> &v) {
    v.assign(10, 0);
    std::size_t n = 0;
    return v[n - 1];  // dtt-error {{out of bounds}}
}

// Variant 3: Heartbleed: the last element of an empty vector.
int variant3_empty_last() {
    std::vector<int> v;
    return v[v.size() - 1];  // dtt-error {{out of bounds}}
}

// Variant 4: a symbolic size_t in [0, 10] minus 1.
int variant4_symbolic_size_t(std::size_t n) {
    std::vector<int> v(10);
    if (n <= 10) return v[n - 1];  // dtt-error {{out of bounds|cannot verify}}
    return 0;
}

// Variant 5: a narrowing initialization: (signed char)200 is -56.
void variant5_narrow_init() {
    signed char c = 200;
    take(c);  // dtt-error {{_value|cannot verify}}
}

// Variant 6: long to int: 2147483648 is INT_MIN.
void variant6_long_to_int() {
    long l = 2147483648L;
    int x = l;
    take(x);  // dtt-error {{_value|cannot verify}}
}

// Variant 7: an unsigned argument converted to int: 4294967295u is -1.
void variant7_unsigned_argument() {
    unsigned u = 4294967295u;
    take(u);  // dtt-error {{_value|cannot verify}}
}

// Variant 8: a narrowing assignment.
void variant8_narrow_assign() {
    signed char c;
    c = 200;
    take(c);  // dtt-error {{_value|cannot verify}}
}

// Variant 9: an unsigned constant converted to int.
void variant9_unsigned_constant() {
    int k = 3000000000u;
    take(k);  // dtt-error {{_value|cannot verify}}
}

// Variant 10: a long long constant narrowed to an int argument.
void variant10_narrow_constant_argument() {
    take(3000000000LL);  // dtt-error {{_value|cannot verify}}
}

// Variant 11: 1 << 31 in an int is INT_MIN.
void variant11_shift_sign() {
    int s = 31;
    int x = 1 << s;
    take(x);  // dtt-error {{_value|cannot verify}}
}

// Variant 12: unsigned i - j wraps when i < j.
int variant12_unsigned_difference(unsigned i, unsigned j) {
    if (i < 10 && j < 10) return a[i - j];  // dtt-error {{out of bounds|cannot verify}}
    return 0;
}

// Variant 13: a mixed-sign comparison converts x to unsigned; x == -1 passes.
void variant13_mixed_sign(int x) {
    unsigned u = 10;
    if (x > u) take(x);  // dtt-error {{_value|cannot verify}}
}

// Variant 14: narrow values wrap on ++ and --.
int a100[100];
int a300[300];
int variant14_narrow_step() {
    signed char c = -128;
    --c;  // 127
    unsigned char u = 255;
    ++u;  // 0
    return a100[c + 129] + a300[u - 1];  // dtt-error {{out of bounds}}
}

// Variant 15: a narrowing copy does not keep what was proven of its source:
// x == -1 passes x < 4 and makes y 255.
void variant15_narrowing_copy_bound(int x) {
    int a[4] = {};
    if (x < 4) {
        unsigned char y = x;
        a[y] = 1;  // dtt-error {{cannot verify|out of bounds|_size}}
    }
}

// Variant 16: the same by assignment, and into a signed narrower type.
void variant16_narrowing_assign_bound(long x) {
    int a[4] = {};
    if (x < 4 && x >= 0) {
        unsigned char ok = x;
        a[ok] = 1;
    }
    if (x < 4) {
        unsigned short y;
        y = static_cast<unsigned short>(x);
        a[y] = 1;  // dtt-error {{cannot verify|out of bounds|_size}}
    }
}

int main() { return 0; }
