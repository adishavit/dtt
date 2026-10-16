// 1743: Loops that cannot exit for some values of their variable
//
// No call or loop below can start in a non-exit set, or no non-exit set
// exists. Runs in enforce mode, so a call or loop DTT cannot verify fails
// the test.
//
// Variant 1 is the fix for CVE-2026-27171 in zlib 1.3.2 (commit ba829a4):
// the public entry points reject negative lengths. Extracted from zlib 1.3.2
// crc32.c (zlib license, (C) 1995-2026 Jean-loup Gailly and Mark Adler).

typedef unsigned int z_crc_t;
typedef long long z_off64_t;
typedef unsigned long uLong;

static const z_crc_t x2n_table[32] = {0};
z_crc_t multmodp(z_crc_t a, z_crc_t b);

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

// Variant 1: zlib 1.3.2.
uLong crc32_combine64(uLong crc1, uLong crc2, z_off64_t len2) {
    if (len2 < 0)
        return 0;
    return multmodp(x2nmodp(len2, 3), crc1) ^ (crc2 & 0xffffffff);
}

uLong crc32_combine_gen64(z_off64_t len2) {
    if (len2 < 0)
        return 0;
    return x2nmodp(len2, 3);
}

static int bit_length(int x) {
    int r = 0;
    while (x != 0) {
        x >>= 1;
        ++r;
    }
    return r;
}

// Variant 2: calls that satisfy the precondition.
int variant2_safe_calls(int n) {
    int r = bit_length(5) + bit_length(0);
    if (n < 0) return r;
    return r + bit_length(n);
}

// Variant 3: unsigned values reach zero by shifting.
int variant3_unsigned(unsigned u) {
    int r = 0;
    while (u) {
        u >>= 1;
        ++r;
    }
    return r;
}

// Variant 4: a guard before the loop excludes the non-exit set.
int variant4_guarded(int x) {
    if (x < 0) return 0;
    int r = 0;
    while (x) {
        x >>= 1;
        ++r;
    }
    return r;
}

// Variant 5: C division truncates toward zero, so x /= 2 reaches 0 from below.
int variant5_divide(int x) {
    int r = 0;
    while (x) {
        x /= 2;
        ++r;
    }
    return r;
}

// Variant 6: doubling a negative value overflows instead of staying negative.
int variant6_shift_left(int x) {
    int r = 0;
    while (x < 0) {
        x *= 2;
        ++r;
    }
    return r;
}

// Variant 7: a loop with another exit.
int variant7_break(int x) {
    int r = 0;
    while (x) {
        if (r > 64) break;
        x >>= 1;
        ++r;
    }
    return r;
}

// Variant 8: a counted loop and a loop whose variable is not updated arithmetically.
bool poll();
int variant8_other_loops(int n) {
    int r = 0;
    for (int i = 0; i < n; ++i) r += i;
    bool running = true;
    while (running) running = poll();
    return r;
}

// Variant 9: x > 0 is left by shifting; there is no negative x in the loop.
int variant9_positive(int x) {
    int r = 0;
    while (x > 0) {
        x >>= 1;
        ++r;
    }
    return r;
}

// Variant 10: unsigned char is promoted, shifted and converted back.
int variant10_unsigned_char(unsigned char c) {
    int r = 0;
    while (c) {
        c >>= 1;
        ++r;
    }
    return r;
}

// Variant 11: a constant initializer outside the non-exit set.
int variant11_constant_init() {
    int acc = 0;
    for (int i = 16; i != 0; i >>= 1)
        acc += i;
    return acc;
}

int main() { return 0; }
