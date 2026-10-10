// zlib deflate lifecycle — incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches misuse of the deflate stream lifecycle.
// Each variant triggers a precondition violation on @stream_state.

#include <zlib.h>
#include <cstring>

// Variant 1: deflate after deflateEnd — stream no longer active
void variant_use_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Data to compress";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflateEnd(&strm);                           // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 1, but @stream_state == 0
    deflate(&strm, Z_FINISH);  // dtt-error {{requires @stream_state}}
}

// Variant 2: deflateReset after deflateEnd — stream already ended
void variant_reset_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflateEnd(&strm);                           // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 1, but @stream_state == 0
    deflateReset(&strm);  // dtt-error {{requires @stream_state}}
}

int main() {
    variant_use_after_end();
    variant_reset_after_end();
    return 0;
}
