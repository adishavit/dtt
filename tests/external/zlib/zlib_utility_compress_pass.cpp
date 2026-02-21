// zlib utility functions — compress/uncompress correct usage (should COMPILE)
//
// Tests the stateless compress/uncompress utility functions. These have no
// lifecycle state — DTT only checks that pointer arguments are non-null.

#include <zlib.h>
#include <cstring>

// Variant 1: basic compress and uncompress round-trip
void variant_compress_roundtrip() {
    const char source[] = "Hello, zlib compress/uncompress!";
    uLong sourceLen = sizeof(source);

    unsigned char compressed[256];
    uLong compressedLen = sizeof(compressed);

    compress(compressed, &compressedLen,
             reinterpret_cast<const Bytef*>(source), sourceLen);

    char decompressed[256];
    uLong decompressedLen = sizeof(decompressed);

    uncompress(reinterpret_cast<Bytef*>(decompressed), &decompressedLen,
               compressed, compressedLen);
}

// Variant 2: compress2 with explicit compression level
void variant_compress2() {
    const char source[] = "Testing compress2 with best compression";
    uLong sourceLen = sizeof(source);

    unsigned char dest[256];
    uLong destLen = sizeof(dest);

    compress2(dest, &destLen,
              reinterpret_cast<const Bytef*>(source), sourceLen,
              Z_BEST_COMPRESSION);
}

// Variant 3: compressBound for safe buffer sizing
void variant_compress_bound() {
    const char source[] = "Using compressBound for allocation";
    uLong sourceLen = sizeof(source);

    uLong bound = compressBound(sourceLen);
    unsigned char dest[512];
    uLong destLen = bound;

    compress(dest, &destLen,
             reinterpret_cast<const Bytef*>(source), sourceLen);
}

int main() {
    variant_compress_roundtrip();
    variant_compress2();
    variant_compress_bound();
    return 0;
}
