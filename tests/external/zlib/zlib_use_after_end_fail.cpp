// zlib use-after-end — deflate/inflate operations after End (should FAIL to compile)
//
// Tests that DTT catches various stream operations performed after the
// stream has been ended. Covers both deflate and inflate families.

#include <zlib.h>
#include <cstring>

// Variant 1: deflateBound after deflateEnd
void variant_deflate_bound_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflateEnd(&strm);                           // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 1, but @stream_state == 0
    deflateBound(&strm, 1024);  // dtt-error {{requires @stream_state}}
}

// Variant 2: inflateSync after inflateEnd
void variant_inflate_sync_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    inflateInit(&strm);                          // @stream_state = 2
    inflateEnd(&strm);                           // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 2, but @stream_state == 0
    inflateSync(&strm);  // dtt-error {{requires @stream_state}}
}

// Variant 3: deflateSetDictionary after deflateEnd
void variant_deflate_dict_after_end() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char dict[] = "custom dictionary data";

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1
    deflateEnd(&strm);                           // @stream_state = 0

    // DTT ERROR: requires arg0.@stream_state == 1, but @stream_state == 0
    deflateSetDictionary(&strm, dict, sizeof(dict));  // dtt-error {{requires @stream_state}}
}

int main() {
    variant_deflate_bound_after_end();
    variant_inflate_sync_after_end();
    variant_deflate_dict_after_end();
    return 0;
}
