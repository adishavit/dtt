// zlib stream copy — deflateCopy and inflateCopy correct usage (should COMPILE)
//
// Tests that DTT tracks @stream_state propagation through stream copy
// operations. The destination stream inherits the source's active state
// and must also be properly ended before scope exit.

#include <zlib.h>
#include <cstring>

// Variant 1: deflateCopy — duplicate an active deflate stream
void variant_deflate_copy() {
    z_stream src;
    memset(&src, 0, sizeof(src));

    unsigned char in[]  = "Data to compress with copy";
    unsigned char out[256];

    src.next_in   = in;
    src.avail_in  = sizeof(in);
    src.next_out  = out;
    src.avail_out = sizeof(out);

    deflateInit(&src, Z_DEFAULT_COMPRESSION);  // src.@stream_state = 1

    z_stream dst;
    memset(&dst, 0, sizeof(dst));
    deflateCopy(&dst, &src);                   // dst.@stream_state = 1 (copied)

    deflateEnd(&dst);                          // dst.@stream_state = 0
    deflateEnd(&src);                          // src.@stream_state = 0
    // scope_exit: both streams at @stream_state == 0 — satisfied
}

// Variant 2: inflateCopy — duplicate an active inflate stream
void variant_inflate_copy() {
    z_stream src;
    memset(&src, 0, sizeof(src));

    unsigned char in[64]  = {0};
    unsigned char out[256];

    src.next_in   = in;
    src.avail_in  = sizeof(in);
    src.next_out  = out;
    src.avail_out = sizeof(out);

    inflateInit(&src);                         // src.@stream_state = 2

    z_stream dst;
    memset(&dst, 0, sizeof(dst));
    inflateCopy(&dst, &src);                   // dst.@stream_state = 2 (copied)

    inflateEnd(&dst);                          // dst.@stream_state = 0
    inflateEnd(&src);                          // src.@stream_state = 0
}

int main() {
    variant_deflate_copy();
    variant_inflate_copy();
    return 0;
}
