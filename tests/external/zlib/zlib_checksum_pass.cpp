// zlib checksum functions -- correct usage patterns (should COMPILE)
//
// Tests the stateless checksum functions: crc32, crc32_z, crc32_combine,
// adler32, adler32_z, adler32_combine. These functions have no preconditions
// in the DTT annotations (no stream state, no nonnull requirements on the
// data pointer since NULL means "initial value" in zlib's API).

#include <zlib.h>

// Variant 1: crc32 -- basic CRC-32 computation
void variant_crc32() {
    const unsigned char data[] = "Hello, zlib CRC-32!";
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, data, sizeof(data) - 1);
}

// Variant 2: crc32 -- incremental computation across multiple buffers
void variant_crc32_incremental() {
    const unsigned char part1[] = "First part";
    const unsigned char part2[] = " Second part";
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, part1, sizeof(part1) - 1);
    crc = crc32(crc, part2, sizeof(part2) - 1);
}

// Variant 3: crc32_z -- size_t length variant
void variant_crc32_z() {
    const unsigned char data[] = "Testing crc32_z with z_size_t length";
    uLong crc = crc32_z(0L, Z_NULL, 0);
    crc = crc32_z(crc, data, sizeof(data) - 1);
}

// Variant 4: crc32_combine -- combine two CRC values
void variant_crc32_combine() {
    const unsigned char buf1[] = "First buffer";
    const unsigned char buf2[] = "Second buffer";
    uLong crc1 = crc32(0L, buf1, sizeof(buf1) - 1);
    uLong crc2 = crc32(0L, buf2, sizeof(buf2) - 1);
    uLong combined = crc32_combine(crc1, crc2, sizeof(buf2) - 1);
    (void)combined;
}

// Variant 5: adler32 -- basic Adler-32 computation
void variant_adler32() {
    const unsigned char data[] = "Hello, zlib Adler-32!";
    uLong adler = adler32(0L, Z_NULL, 0);
    adler = adler32(adler, data, sizeof(data) - 1);
}

// Variant 6: adler32 -- incremental computation
void variant_adler32_incremental() {
    const unsigned char part1[] = "Adler part one";
    const unsigned char part2[] = " Adler part two";
    uLong adler = adler32(0L, Z_NULL, 0);
    adler = adler32(adler, part1, sizeof(part1) - 1);
    adler = adler32(adler, part2, sizeof(part2) - 1);
}

// Variant 7: adler32_z -- size_t length variant
void variant_adler32_z() {
    const unsigned char data[] = "Testing adler32_z with z_size_t length";
    uLong adler = adler32_z(0L, Z_NULL, 0);
    adler = adler32_z(adler, data, sizeof(data) - 1);
}

// Variant 8: adler32_combine -- combine two Adler-32 values
void variant_adler32_combine() {
    const unsigned char buf1[] = "First Adler buffer";
    const unsigned char buf2[] = "Second Adler buffer";
    uLong a1 = adler32(0L, buf1, sizeof(buf1) - 1);
    uLong a2 = adler32(0L, buf2, sizeof(buf2) - 1);
    uLong combined = adler32_combine(a1, a2, sizeof(buf2) - 1);
    (void)combined;
}

int main() {
    variant_crc32();
    variant_crc32_incremental();
    variant_crc32_z();
    variant_crc32_combine();
    variant_adler32();
    variant_adler32_incremental();
    variant_adler32_z();
    variant_adler32_combine();
    return 0;
}
