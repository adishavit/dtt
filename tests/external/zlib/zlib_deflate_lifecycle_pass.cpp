// zlib deflate lifecycle — correct usage patterns (should COMPILE)
//
// Tests that the deflateInit -> deflate -> deflateEnd lifecycle is tracked
// correctly by DTT ghost field @stream_state. Each variant properly
// initializes, uses, and ends the deflate stream before scope exit.

#include <zlib.h>
#include <cstring>

// Variant 1: Basic deflateInit -> deflate -> deflateEnd
void variant_basic_deflate() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Hello, zlib compression!";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflate(&strm, Z_FINISH);                   // requires @stream_state == 1
    deflateEnd(&strm);                           // @stream_state = 0
    // scope_exit: @stream_state == 0 — satisfied
}

// Variant 2: deflateInit2 with custom parameters
void variant_deflateInit2() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Testing deflateInit2 with raw deflate";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit2(&strm, Z_BEST_SPEED, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
}

// Variant 3: deflateInit -> deflateReset -> deflate -> deflateEnd
void variant_deflate_with_reset() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Reset and reuse the deflate stream";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflateReset(&strm);                         // requires 1, ensures 1

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflate(&strm, Z_FINISH);                   // requires @stream_state == 1
    deflateEnd(&strm);                           // @stream_state = 0
}

int main() {
    variant_basic_deflate();
    variant_deflateInit2();
    variant_deflate_with_reset();
    return 0;
}
