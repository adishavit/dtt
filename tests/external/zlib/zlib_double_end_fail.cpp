// zlib double-end — calling End twice on streams (should FAIL to compile)
//
// Calling deflateEnd or inflateEnd twice is a classic double-free pattern.
// DTT catches this because the second End call requires @stream_state to be
// active, but the first End already reset it to 0.

#include <zlib.h>
#include <cstring>

// Variant 1: double deflateEnd
void variant_double_deflate_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflateEnd(&strm);                           // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 1, but @stream_state == 0
    deflateEnd(&strm);  // dtt-error {{requires @stream_state}}
}

// Variant 2: double inflateEnd
void variant_double_inflate_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    inflateInit(&strm);                          // @stream_state = 2
    inflateEnd(&strm);                           // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateEnd(&strm);  // dtt-error {{requires @stream_state}}
}

int main() {
    variant_double_deflate_end();
    variant_double_inflate_end();
    return 0;
}
