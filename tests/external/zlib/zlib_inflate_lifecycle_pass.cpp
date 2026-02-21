// zlib inflate lifecycle — correct usage patterns (should COMPILE)
//
// Tests that the inflateInit -> inflate -> inflateEnd lifecycle is tracked
// correctly by DTT ghost field @stream_state (value 2 = inflate active).

#include <zlib.h>
#include <cstring>

// Variant 1: Basic inflateInit -> inflate -> inflateEnd
void variant_basic_inflate() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // Minimal compressed payload (not valid data, but sufficient for DTT analysis)
    unsigned char in[64]  = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit(&strm);               // @stream_state = 2
    inflate(&strm, Z_NO_FLUSH);       // requires @stream_state == 2
    inflateEnd(&strm);                // @stream_state = 0
    // scope_exit: @stream_state == 0 — satisfied
}

// Variant 2: inflateInit2 with inflateReset then inflateEnd
void variant_inflate_init2_reset() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64]  = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit2(&strm, 15 + 32);     // @stream_state = 2 (auto-detect gzip/zlib)
    inflateReset(&strm);              // requires 2, ensures 2
    inflate(&strm, Z_NO_FLUSH);       // requires @stream_state == 2
    inflateEnd(&strm);                // @stream_state = 0
}

int main() {
    variant_basic_inflate();
    variant_inflate_init2_reset();
    return 0;
}
