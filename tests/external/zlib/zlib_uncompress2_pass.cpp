// zlib uncompress2 -- correct usage patterns (should COMPILE)
//
// Tests the uncompress2 utility function, which is like uncompress but also
// returns the consumed source length via a pointer. All four pointer
// arguments require _nonnull != 0.

#include <zlib.h>
#include <cstring>

// Variant 1: basic uncompress2 round-trip with compress + uncompress2
void variant_uncompress2_roundtrip() {
    const char source[] = "Hello, uncompress2 test data!";
    uLong sourceLen = sizeof(source);

    unsigned char compressed[256];
    uLong compressedLen = sizeof(compressed);

    compress(compressed, &compressedLen,
             reinterpret_cast<const Bytef*>(source), sourceLen);

    char decompressed[256];
    uLong decompressedLen = sizeof(decompressed);
    uLong consumedLen = compressedLen;

    uncompress2(reinterpret_cast<Bytef*>(decompressed), &decompressedLen,
                compressed, &consumedLen);
}

// Variant 2: uncompress2 with compressBound-sized destination buffer
void variant_uncompress2_with_bound() {
    const char source[] = "Using compressBound for safe allocation with uncompress2";
    uLong sourceLen = sizeof(source);

    uLong bound = compressBound(sourceLen);
    unsigned char compressed[512];
    uLong compressedLen = bound;

    compress(compressed, &compressedLen,
             reinterpret_cast<const Bytef*>(source), sourceLen);

    char decompressed[512];
    uLong decompressedLen = sizeof(decompressed);
    uLong consumedLen = compressedLen;

    uncompress2(reinterpret_cast<Bytef*>(decompressed), &decompressedLen,
                compressed, &consumedLen);
}

int main() {
    variant_uncompress2_roundtrip();
    variant_uncompress2_with_bound();
    return 0;
}
