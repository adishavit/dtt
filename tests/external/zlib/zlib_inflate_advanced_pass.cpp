// zlib inflate advanced functions -- correct usage patterns (should COMPILE)
//
// Tests advanced inflate functions that require an active inflate stream
// (@stream_state == 2): inflateInit_, inflateInit2_, inflateReset2,
// inflateSetDictionary, inflateMark, inflatePrime, inflateBackInit,
// inflateBackEnd. Also exercises multi-step workflows.

#include <zlib.h>
#include <cstring>

// Variant 1: inflateInit_ -- low-level init with explicit version/size
void variant_inflateInit_lowlevel() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64] = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit_(&strm, ZLIB_VERSION, sizeof(z_stream));  // @stream_state = 2
    inflate(&strm, Z_NO_FLUSH);
    inflateEnd(&strm);                                     // @stream_state = 0
}

// Variant 2: inflateInit2_ -- low-level init2 with explicit version/size
void variant_inflateInit2_lowlevel() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64] = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit2_(&strm, 15, ZLIB_VERSION, sizeof(z_stream));  // @stream_state = 2
    inflate(&strm, Z_NO_FLUSH);
    inflateEnd(&strm);                                          // @stream_state = 0
}

// Variant 3: inflateReset2 -- reset with new window bits
void variant_inflate_reset2() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64] = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit(&strm);                   // @stream_state = 2
    inflateReset2(&strm, 15);             // requires 2, ensures 2
    inflate(&strm, Z_NO_FLUSH);           // requires @stream_state == 2
    inflateEnd(&strm);                    // @stream_state = 0
}

// Variant 4: inflateSetDictionary -- set decompression dictionary
void variant_inflate_set_dictionary() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64]  = {0};
    unsigned char out[256];
    unsigned char dict[]  = "custom dictionary data for inflate";

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit(&strm);                                       // @stream_state = 2
    inflateSetDictionary(&strm, dict, sizeof(dict) - 1);     // requires 2
    inflateEnd(&strm);                                        // @stream_state = 0
}

// Variant 5: inflateMark -- check inflate progress
void variant_inflate_mark() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64] = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit(&strm);                   // @stream_state = 2
    long mark = inflateMark(&strm);       // requires @stream_state == 2
    (void)mark;
    inflateEnd(&strm);                    // @stream_state = 0
}

// Variant 6: inflatePrime -- insert bits into inflate input
void variant_inflate_prime() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64] = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit2(&strm, -15);             // @stream_state = 2 (raw inflate)
    inflatePrime(&strm, 0, 0);            // requires @stream_state == 2
    inflateEnd(&strm);                    // @stream_state = 0
}

// Variant 7: inflateBackInit + inflateBackEnd -- alternative inflate interface
void variant_inflate_back() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char window[1 << 15];  // 32K window for windowBits=15

    inflateBackInit(&strm, 15, window);   // @stream_state = 2
    inflateBackEnd(&strm);                // @stream_state = 0
}

// Variant 8: chained workflow -- init -> set_dict -> mark -> prime -> reset2 -> end
void variant_inflate_all_advanced() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64]  = {0};
    unsigned char out[256];
    unsigned char dict[]  = "shared dictionary for chained test";

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit2(&strm, 15);                                  // @stream_state = 2
    inflateSetDictionary(&strm, dict, sizeof(dict) - 1);
    long mark = inflateMark(&strm);
    (void)mark;
    inflateReset2(&strm, 15);                                 // still @stream_state = 2
    inflate(&strm, Z_NO_FLUSH);
    inflateEnd(&strm);                                        // @stream_state = 0
}

int main() {
    variant_inflateInit_lowlevel();
    variant_inflateInit2_lowlevel();
    variant_inflate_reset2();
    variant_inflate_set_dictionary();
    variant_inflate_mark();
    variant_inflate_prime();
    variant_inflate_back();
    variant_inflate_all_advanced();
    return 0;
}
