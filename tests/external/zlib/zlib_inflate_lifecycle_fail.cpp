// zlib inflate lifecycle — incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches misuse of the inflate stream lifecycle.
// Each variant triggers a precondition violation on @stream_state.

#include <zlib.h>
#include <cstring>

// Variant 1: inflate after inflateEnd — stream already ended
void variant_inflate_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64]  = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit(&strm);                // @stream_state = 2
    inflateEnd(&strm);                 // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflate(&strm, Z_NO_FLUSH);  // dtt-error {{requires @stream_state}}
}

// Variant 2: inflateEnd called twice — double free pattern
void variant_double_inflate_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    inflateInit(&strm);                // @stream_state = 2
    inflateEnd(&strm);                 // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateEnd(&strm);  // dtt-error {{requires @stream_state}}
}

int main() {
    variant_inflate_after_end();
    variant_double_inflate_end();
    return 0;
}
