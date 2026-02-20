// zlib deflate advanced functions -- correct usage patterns (should COMPILE)
//
// Tests advanced deflate functions that require an active deflate stream
// (@stream_state == 1): deflateInit_, deflateInit2_, deflatePending,
// deflatePrime, deflateTune. Also exercises multi-step workflows.

#include <zlib.h>
#include <cstring>

// Variant 1: deflateInit_ -- low-level init with explicit version/size
void variant_deflateInit_lowlevel() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Testing deflateInit_ low-level API";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit_(&strm, Z_DEFAULT_COMPRESSION, ZLIB_VERSION, sizeof(z_stream));
    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
}

// Variant 2: deflateInit2_ -- low-level init2 with explicit version/size
void variant_deflateInit2_lowlevel() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Testing deflateInit2_ with raw deflate";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit2_(&strm, Z_BEST_SPEED, Z_DEFLATED, -15, 8,
                  Z_DEFAULT_STRATEGY, ZLIB_VERSION, sizeof(z_stream));
    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
}

// Variant 3: deflatePending -- query pending output bytes and bits
void variant_deflate_pending() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Data for deflatePending test";
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1

    deflate(&strm, Z_SYNC_FLUSH);

    unsigned pending = 0;
    int bits = 0;
    deflatePending(&strm, &pending, &bits);     // requires @stream_state == 1

    deflateEnd(&strm);                          // @stream_state = 0
}

// Variant 4: deflatePrime -- insert bits into the deflate output
void variant_deflate_prime() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char out[256];
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                 -15, 8, Z_DEFAULT_STRATEGY);   // @stream_state = 1

    deflatePrime(&strm, 3, 0x05);               // requires @stream_state == 1

    deflateEnd(&strm);                           // @stream_state = 0
}

// Variant 5: deflateTune -- adjust internal compression tuning parameters
void variant_deflate_tune() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Data for deflateTune test";
    unsigned char out[512];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);  // @stream_state = 1

    deflateTune(&strm, 4, 4, 8, 4);             // requires @stream_state == 1

    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);                           // @stream_state = 0
}

// Variant 6: chained workflow -- init -> tune -> prime -> pending -> deflate -> end
void variant_deflate_all_advanced() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Combined advanced deflate operations";
    unsigned char out[512];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED,
                 -15, 9, Z_FILTERED);           // @stream_state = 1

    deflateTune(&strm, 8, 16, 32, 16);
    deflatePrime(&strm, 0, 0);

    unsigned pending = 0;
    int bits = 0;
    deflatePending(&strm, &pending, &bits);

    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);                           // @stream_state = 0
}

int main() {
    variant_deflateInit_lowlevel();
    variant_deflateInit2_lowlevel();
    variant_deflate_pending();
    variant_deflate_prime();
    variant_deflate_tune();
    variant_deflate_all_advanced();
    return 0;
}
