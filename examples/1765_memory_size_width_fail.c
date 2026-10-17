/* 1765: memory sizes have the width and signedness of their type (C) */

#include <string.h>

/* Variant 1: 0x80000000u is 2^31, not negative. */
void variant1_large_unsigned(const char *s) {
    char d[16];
    memcpy(d, s, 0x80000000u); /* dtt-error {{memcpy}} */
}

/* Variant 2: (unsigned char)200 bytes into a 100-byte buffer. */
void variant2_narrow_size(const char *s) {
    char d[100];
    memcpy(d, s, (unsigned char)200); /* dtt-error {{memcpy}} */
}

/* Variant 3: memset with 0xFFFFFFFFu bytes. */
void variant3_memset_max(void) {
    char d[16];
    memset(d, 0, 0xFFFFFFFFu); /* dtt-error {{memset|cannot verify}} */
}

/* Variant 4: 16 - k wraps to a huge size when k > 16. */
void variant4_unsigned_size(const char *s, unsigned k) {
    char d[16];
    memcpy(d, s, 16 - k); /* dtt-error {{memcpy}} */
}

int main(void) { return 0; }
