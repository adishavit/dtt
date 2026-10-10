// zlib use-before-init — operations on uninitialized streams (should FAIL to compile)
//
// Tests that DTT catches attempts to use deflate/inflate streams that were
// never initialized. The @stream_state ghost field defaults to 0 (uninitialized).

#include <zlib.h>
#include <cstring>

// Variant 1: deflate without deflateInit — stream never initialized
void variant_deflate_no_init() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Uninitialized deflate";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    // DTT ERROR: requires arg0.@stream_state == 1 (required state is not established in enforce mode)
    deflate(&strm, Z_FINISH);  // dtt-error {{@stream_state}}
}

// Variant 2: inflate without inflateInit — stream never initialized
void variant_inflate_no_init() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64]  = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    // DTT ERROR: requires arg0.@stream_state == 2 (required state is not established in enforce mode)
    inflate(&strm, Z_NO_FLUSH);  // dtt-error {{@stream_state}}
}

int main() {
    variant_deflate_no_init();
    variant_inflate_no_init();
    return 0;
}
