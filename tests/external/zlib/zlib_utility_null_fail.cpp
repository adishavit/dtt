// zlib utility functions — null pointer arguments (should FAIL to compile)
//
// Tests that DTT catches null pointer arguments to compress/uncompress.
// All buffer pointer parameters require _nonnull != 0.

#include <zlib.h>
#include <cstring>

// Variant 1: compress with null destination
void variant_compress_null_dest() {
    const char source[] = "Data to compress";
    uLong sourceLen = sizeof(source);
    uLong destLen = 256;

    // DTT ERROR: requires arg0._nonnull != 0
    compress(nullptr, &destLen,  // dtt-error {{requires _nonnull}}
             reinterpret_cast<const Bytef*>(source), sourceLen);
}

// Variant 2: uncompress with null source
void variant_uncompress_null_source() {
    unsigned char dest[256];
    uLong destLen = sizeof(dest);

    // DTT ERROR: requires arg2._nonnull != 0
    uncompress(dest, &destLen, nullptr, 0);  // dtt-error {{requires _nonnull}}
}

// Variant 3: compress with null destLen pointer
void variant_compress_null_destlen() {
    const char source[] = "Data to compress";
    uLong sourceLen = sizeof(source);
    unsigned char dest[256];

    // DTT ERROR: requires arg1._nonnull != 0
    compress(dest, nullptr,  // dtt-error {{requires _nonnull}}
             reinterpret_cast<const Bytef*>(source), sourceLen);
}

int main() {
    variant_compress_null_dest();
    variant_uncompress_null_source();
    variant_compress_null_destlen();
    return 0;
}
