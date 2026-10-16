// 1743: Loops that cannot exit for some values of their variable
//
// A loop whose condition is `v OP constant` never exits once v is in a set S
// that implies the condition and that every write to v in the loop maps
// back into S. For a loop at the top of a function over a parameter, DTT
// records "requires not S" as the function's precondition and checks it at
// each call: a violating value is an error, an unknown one "cannot verify".
// Any other such loop is checked where it starts.
//
// Variant 1 is CVE-2026-27171 (zlib < 1.3.2, madler/zlib#904): x2nmodp()
// shifts a signed length right until it is zero; a negative length stays
// negative forever. Extracted from zlib 1.3.1 crc32.c (zlib license,
// (C) 1995-2026 Jean-loup Gailly and Mark Adler); macros expanded.

typedef unsigned int z_crc_t;
typedef long long z_off64_t;
typedef long z_off_t;
typedef unsigned long uLong;

#define POLY 0xedb88320

static const z_crc_t x2n_table[32] = {0};

static z_crc_t multmodp(z_crc_t a, z_crc_t b) {
    z_crc_t m, p;

    m = (z_crc_t)1 << 31;
    p = 0;
    for (;;) {
        if (a & m) {
            p ^= b;
            if ((a & (m - 1)) == 0)
                break;
        }
        m >>= 1;
        b = b & 1 ? (b >> 1) ^ POLY : b >> 1;
    }
    return p;
}

// Variant 1: zlib 1.3.1 passes the caller's length through unchecked.
static z_crc_t x2nmodp(z_off64_t n, unsigned k) {
    z_crc_t p;

    p = (z_crc_t)1 << 31;           /* x^0 == 1 */
    while (n) {
        if (n & 1)
            p = multmodp(x2n_table[k & 31], p);
        n >>= 1;
        k++;
    }
    return p;
}

uLong crc32_combine64(uLong crc1, uLong crc2, z_off64_t len2) {
    return multmodp(x2nmodp(len2, 3), crc1) ^ (crc2 & 0xffffffff);  // dtt-error {{cannot verify arg0 >= 0}}
}

uLong crc32_combine(uLong crc1, uLong crc2, z_off_t len2) {
    return crc32_combine64(crc1, crc2, (z_off64_t)len2);
}

uLong crc32_combine_gen64(z_off64_t len2) {
    return x2nmodp(len2, 3);  // dtt-error {{cannot verify arg0 >= 0}}
}

// Shared helper: never exits for negative x.
static int bit_length(int x) {
    int r = 0;
    while (x != 0) {
        x >>= 1;
        ++r;
    }
    return r;
}

// Variant 2: a constant argument in the non-exit set.
int variant2_constant_argument() {
    return bit_length(-8);  // dtt-error {{requires arg0 >= 0}}
}

// Variant 3: an argument DTT cannot establish.
int read_value();
int variant3_unknown_argument() {
    return bit_length(read_value());  // dtt-error {{cannot verify arg0 >= 0}}
}

// Variant 4: a guard that excludes only part of the non-exit set.
int variant4_partial_guard(int x) {
    if (x < -5) return 0;
    return bit_length(x);  // dtt-error {{cannot verify arg0 >= 0}}
}

// Variant 5: signed char is promoted, shifted and converted back; -1 stays -1.
static int char_bits(signed char c) {
    int r = 0;
    while (c) {
        c >>= 1;
        ++r;
    }
    return r;
}
int variant5_signed_char() {
    return char_bits(-1);  // dtt-error {{requires arg0 >= 0}}
}

// Variant 6: do-while form.
static int do_bits(int x) {
    int r = 0;
    do {
        x >>= 1;
        ++r;
    } while (x != 0);
    return r;
}
int variant6_do_while() {
    return do_bits(-3);  // dtt-error {{requires arg0 >= 0}}
}

// Variant 7: the loop's own initializer is in the non-exit set.
int variant7_constant_init() {
    int acc = 0;
    for (int i = -16; i != 0; i >>= 1)  // dtt-error {{loop never exits}}
        acc += i;
    return acc;
}

// Variant 8: a local assigned a value in the non-exit set.
int variant8_constant_local() {
    int x = -1;
    int r = 0;
    while (x) {  // dtt-error {{loop never exits}}
        x >>= 1;
        ++r;
    }
    return r;
}

// Variant 9: the loop variable comes from a value DTT cannot establish.
int variant9_unknown_local() {
    int x = read_value();
    int r = 0;
    while (x) {  // dtt-error {{cannot verify}}
        x >>= 1;
        ++r;
    }
    return r;
}

int main() { return 0; }
