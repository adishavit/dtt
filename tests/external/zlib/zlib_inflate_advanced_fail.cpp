// zlib inflate advanced functions -- incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches state violations for advanced inflate operations
// (inflateReset2, inflateSetDictionary, inflateMark, inflatePrime,
// inflateBackEnd) when the stream is not in the correct state.

#include <zlib.h>
#include <cstring>

// Variant 1: inflateReset2 on uninitialized stream
void variant_reset2_no_init() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateReset2(&strm, 15);
}

// Variant 2: inflateSetDictionary after inflateEnd
void variant_setdict_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char dict[] = "custom dictionary";

    inflateInit(&strm);                                  // @stream_state = 2
    inflateEnd(&strm);                                   // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateSetDictionary(&strm, dict, sizeof(dict));
}

// Variant 3: inflateMark on uninitialized stream
void variant_mark_no_init() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateMark(&strm);
}

// Variant 4: inflatePrime after inflateEnd
void variant_prime_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    inflateInit2(&strm, -15);                            // @stream_state = 2
    inflateEnd(&strm);                                   // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflatePrime(&strm, 0, 0);
}

// Variant 5: inflateBackEnd on uninitialized stream
void variant_back_end_no_init() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateBackEnd(&strm);
}

// Variant 6: inflateCopy from an ended source stream
void variant_copy_ended_source() {
    z_stream src;
    memset(&src, 0, sizeof(src));

    inflateInit(&src);                                   // @stream_state = 2
    inflateEnd(&src);                                    // @stream_state = 0

    z_stream dst;
    memset(&dst, 0, sizeof(dst));

    // DTT ERROR: requires arg1.@stream_state == 2, but @stream_state == 0
    inflateCopy(&dst, &src);
}

int main() {
    variant_reset2_no_init();
    variant_setdict_after_end();
    variant_mark_no_init();
    variant_prime_after_end();
    variant_back_end_no_init();
    variant_copy_ended_source();
    return 0;
}
