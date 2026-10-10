// zlib deflate advanced functions -- incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches state violations for advanced deflate operations
// (deflatePending, deflatePrime, deflateTune) when the stream is not in
// the correct state.

#include <zlib.h>
#include <cstring>

// Variant 1: deflatePending on uninitialized stream
void variant_pending_no_init() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned pending = 0;
    int bits = 0;

    // DTT ERROR: requires arg0.@stream_state == 1 (required state is not established in enforce mode)
    deflatePending(&strm, &pending, &bits);  // dtt-error {{@stream_state}}
}

// Variant 2: deflatePrime after deflateEnd
void variant_prime_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char out[256];
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);   // @stream_state = 1
    deflateEnd(&strm);                            // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 1, but @stream_state == 0
    deflatePrime(&strm, 3, 0x05);  // dtt-error {{requires @stream_state}}
}

// Variant 3: deflateTune on uninitialized stream
void variant_tune_no_init() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // DTT ERROR: requires arg0.@stream_state == 1 (required state is not established in enforce mode)
    deflateTune(&strm, 4, 4, 8, 4);  // dtt-error {{@stream_state}}
}

// Variant 4: deflateCopy from an ended source stream
void variant_copy_ended_source() {
    z_stream src;
    memset(&src, 0, sizeof(src));

    deflateInit(&src, Z_DEFAULT_COMPRESSION);    // @stream_state = 1
    deflateEnd(&src);                             // @stream_state = 0

    z_stream dst;
    memset(&dst, 0, sizeof(dst));

    // DTT ERROR: requires arg1.@stream_state == 1, but @stream_state == 0
    deflateCopy(&dst, &src);  // dtt-error {{requires @stream_state}}
}

int main() {
    variant_pending_no_init();
    variant_prime_after_end();
    variant_tune_no_init();
    variant_copy_ended_source();
    return 0;
}
