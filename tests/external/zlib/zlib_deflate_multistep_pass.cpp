// zlib deflate multi-step workflows -- correct usage patterns (should COMPILE)
//
// Exercises complete multi-step state machine workflows for the deflate
// lifecycle. Walks through all state transitions:
//   @stream_state: 0 -> 1 (init) -> 1 (operations) -> 0 (end)
// Also tests reset cycles and copy-based branching.

#include <zlib.h>
#include <cstring>

// Variant 1: init -> deflate -> reset -> deflate -> end
//   State: 0 -> 1 -> (deflate, stays 1) -> (reset, stays 1) -> (deflate, stays 1) -> 0
void variant_deflate_reset_cycle() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in1[] = "First compression pass";
    unsigned char in2[] = "Second compression pass after reset";
    unsigned char out[512];

    strm.next_in   = in1;
    strm.avail_in  = sizeof(in1);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);   // 0 -> 1
    deflate(&strm, Z_FINISH);                    // requires 1

    deflateReset(&strm);                         // requires 1, ensures 1

    strm.next_in   = in2;
    strm.avail_in  = sizeof(in2);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflate(&strm, Z_FINISH);                    // requires 1
    deflateEnd(&strm);                           // 1 -> 0
}

// Variant 2: init -> setdict -> params -> deflate -> reset -> deflate -> end
//   Exercises dictionary setting and parameter changes between compression runs
void variant_deflate_dict_params_reset() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char dict[] = "custom dictionary";
    unsigned char in[]   = "Data compressed with dictionary and custom params";
    unsigned char out[512];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateInit(&strm, Z_DEFAULT_COMPRESSION);         // 0 -> 1
    deflateSetDictionary(&strm, dict, sizeof(dict));   // requires 1
    deflateParams(&strm, Z_BEST_SPEED, Z_FILTERED);   // requires 1
    deflate(&strm, Z_SYNC_FLUSH);                      // requires 1

    deflateReset(&strm);                               // requires 1, ensures 1

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    deflateSetDictionary(&strm, dict, sizeof(dict));
    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);                                 // 1 -> 0
}

// Variant 3: init -> copy -> deflate both -> end both
//   Two parallel deflate streams from a copy operation
void variant_deflate_copy_and_diverge() {
    z_stream src;
    memset(&src, 0, sizeof(src));

    unsigned char in_src[] = "Source stream data";
    unsigned char in_dst[] = "Destination stream divergent data";
    unsigned char out_src[512];
    unsigned char out_dst[512];

    src.next_in   = in_src;
    src.avail_in  = sizeof(in_src);
    src.next_out  = out_src;
    src.avail_out = sizeof(out_src);

    deflateInit(&src, Z_DEFAULT_COMPRESSION);    // src: 0 -> 1

    z_stream dst;
    memset(&dst, 0, sizeof(dst));
    deflateCopy(&dst, &src);                     // dst: 0 -> 1, requires src == 1

    // Deflate the source stream
    deflate(&src, Z_FINISH);

    // Reset and reuse the destination independently
    deflateReset(&dst);                          // dst stays 1
    dst.next_in   = in_dst;
    dst.avail_in  = sizeof(in_dst);
    dst.next_out  = out_dst;
    dst.avail_out = sizeof(out_dst);
    deflate(&dst, Z_FINISH);

    deflateEnd(&dst);                            // dst: 1 -> 0
    deflateEnd(&src);                            // src: 1 -> 0
}

// Variant 4: init2 -> bound -> tune -> multiple deflate calls -> end
//   Progressive compression with intermediate queries
void variant_deflate_progressive() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Progressive deflate with multiple flush points";
    unsigned char out[1024];

    deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED,
                 15, 9, Z_DEFAULT_STRATEGY);     // 0 -> 1

    uLong bound = deflateBound(&strm, sizeof(in));  // requires 1
    (void)bound;

    deflateTune(&strm, 8, 16, 32, 16);             // requires 1

    // First chunk
    strm.next_in   = in;
    strm.avail_in  = 10;
    strm.next_out  = out;
    strm.avail_out = sizeof(out);
    deflate(&strm, Z_NO_FLUSH);                     // requires 1

    // Second chunk
    strm.next_in   = in + 10;
    strm.avail_in  = sizeof(in) - 10;
    deflate(&strm, Z_FINISH);                       // requires 1

    deflateEnd(&strm);                              // 1 -> 0
}

// Variant 5: three sequential init-deflate-end cycles on the same variable
//   Demonstrates proper re-initialization after End
void variant_deflate_reinit_cycles() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[]  = "Repeated init/end cycles";
    unsigned char out[256];

    // Cycle 1
    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);
    deflateInit(&strm, Z_BEST_SPEED);       // 0 -> 1
    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);                       // 1 -> 0

    // Cycle 2
    memset(&strm, 0, sizeof(strm));
    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);
    deflateInit(&strm, Z_BEST_COMPRESSION);  // 0 -> 1
    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);                       // 1 -> 0

    // Cycle 3
    memset(&strm, 0, sizeof(strm));
    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);
    deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                 15 + 16, 8, Z_DEFAULT_STRATEGY);  // gzip format, 0 -> 1
    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);                       // 1 -> 0
}

int main() {
    variant_deflate_reset_cycle();
    variant_deflate_dict_params_reset();
    variant_deflate_copy_and_diverge();
    variant_deflate_progressive();
    variant_deflate_reinit_cycles();
    return 0;
}
