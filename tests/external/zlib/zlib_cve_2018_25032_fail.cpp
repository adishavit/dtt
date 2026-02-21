// CVE-2018-25032: deflate operations after stream teardown (should FAIL to compile)
//
// CVE-2018-25032 involves memory corruption in zlib's deflate when certain
// internal state is corrupted. A common trigger pattern is reusing a deflate
// stream after deflateEnd, leading to use-after-free of internal buffers.
// DTT catches this because deflate requires @stream_state == 1.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2018-25032

#include <zlib.h>
#include <cstring>

// Variant 1: deflate after deflateEnd — use-after-free of internal deflate state
void variant_deflate_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Crafted input triggering memory corruption";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflate(&strm, Z_SYNC_FLUSH);               // OK
    deflateEnd(&strm);                           // @stream_state = 0

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    // DTT ERROR: requires arg0.@stream_state == 1, but @stream_state == 0
    deflate(&strm, Z_FINISH);
}

// Variant 2: deflateParams after deflateEnd — reconfiguring a dead stream
void variant_params_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflateEnd(&strm);                           // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 1, but @stream_state == 0
    deflateParams(&strm, Z_BEST_COMPRESSION, Z_HUFFMAN_ONLY);
}

int main() {
    variant_deflate_after_end();
    variant_params_after_end();
    return 0;
}
