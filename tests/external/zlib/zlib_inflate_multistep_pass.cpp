// zlib inflate multi-step workflows -- correct usage patterns (should COMPILE)
//
// Exercises complete multi-step state machine workflows for the inflate
// lifecycle. Walks through all state transitions:
//   @stream_state: 0 -> 2 (init) -> 2 (operations) -> 0 (end)
// Also tests reset cycles, copy-based branching, and the back-inflate API.

#include <zlib.h>
#include <cstring>

// Variant 1: init -> inflate -> reset -> inflate -> end
//   State: 0 -> 2 -> (inflate, stays 2) -> (reset, stays 2) -> (inflate, stays 2) -> 0
void variant_inflate_reset_cycle() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64] = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit(&strm);                   // 0 -> 2
    inflate(&strm, Z_NO_FLUSH);           // requires 2

    inflateReset(&strm);                  // requires 2, ensures 2

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflate(&strm, Z_NO_FLUSH);           // requires 2
    inflateEnd(&strm);                    // 2 -> 0
}

// Variant 2: init2 -> reset2 -> setdict -> inflate -> sync -> end
//   Exercises dictionary and sync operations within the inflate lifecycle
void variant_inflate_dict_sync_reset() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64]  = {0};
    unsigned char out[256];
    unsigned char dict[]  = "dictionary for inflate multistep test";

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit2(&strm, 15);                                   // 0 -> 2
    inflateReset2(&strm, 15);                                  // requires 2, ensures 2
    inflateSetDictionary(&strm, dict, sizeof(dict) - 1);      // requires 2
    inflate(&strm, Z_NO_FLUSH);                                // requires 2
    inflateSync(&strm);                                        // requires 2
    inflateEnd(&strm);                                         // 2 -> 0
}

// Variant 3: init -> copy -> end both
//   Two parallel inflate streams from a copy operation
void variant_inflate_copy_and_diverge() {
    z_stream src;
    memset(&src, 0, sizeof(src));

    unsigned char in[64] = {0};
    unsigned char out[256];

    src.next_in   = in;
    src.avail_in  = sizeof(in);
    src.next_out  = out;
    src.avail_out = sizeof(out);

    inflateInit(&src);                     // src: 0 -> 2

    z_stream dst;
    memset(&dst, 0, sizeof(dst));
    inflateCopy(&dst, &src);               // dst: 0 -> 2, requires src == 2

    // Use source
    inflate(&src, Z_NO_FLUSH);

    // Reset and reuse destination independently
    inflateReset(&dst);                    // dst stays 2
    unsigned char in2[64] = {0};
    unsigned char out2[256];
    dst.next_in   = in2;
    dst.avail_in  = sizeof(in2);
    dst.next_out  = out2;
    dst.avail_out = sizeof(out2);
    inflate(&dst, Z_NO_FLUSH);

    inflateEnd(&dst);                      // dst: 2 -> 0
    inflateEnd(&src);                      // src: 2 -> 0
}

// Variant 4: init -> mark -> getHeader -> inflate -> end
//   Query functions interleaved with inflate operations
void variant_inflate_queries() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64] = {0};
    unsigned char out[256];

    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);

    inflateInit2(&strm, 15 + 16);         // 0 -> 2 (gzip auto-detect)

    long mark = inflateMark(&strm);        // requires 2
    (void)mark;

    gz_header head;
    memset(&head, 0, sizeof(head));
    inflateGetHeader(&strm, &head);        // requires 2

    inflate(&strm, Z_NO_FLUSH);           // requires 2
    inflateEnd(&strm);                    // 2 -> 0
}

// Variant 5: inflateBackInit -> inflateBackEnd cycle
//   Alternative inflate interface with explicit window buffer
void variant_inflate_back_cycle() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char window[1 << 15];

    inflateBackInit(&strm, 15, window);    // 0 -> 2
    inflateBackEnd(&strm);                 // 2 -> 0
}

// Variant 6: three sequential init-inflate-end cycles on the same variable
void variant_inflate_reinit_cycles() {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned char in[64] = {0};
    unsigned char out[256];

    // Cycle 1: basic inflateInit
    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);
    inflateInit(&strm);                    // 0 -> 2
    inflate(&strm, Z_NO_FLUSH);
    inflateEnd(&strm);                     // 2 -> 0

    // Cycle 2: inflateInit2 with raw deflate
    memset(&strm, 0, sizeof(strm));
    strm.next_in   = in;
    strm.avail_in  = sizeof(in);
    strm.next_out  = out;
    strm.avail_out = sizeof(out);
    inflateInit2(&strm, -15);             // 0 -> 2
    inflate(&strm, Z_NO_FLUSH);
    inflateEnd(&strm);                     // 2 -> 0

    // Cycle 3: inflateBackInit/End
    memset(&strm, 0, sizeof(strm));
    unsigned char window[1 << 15];
    inflateBackInit(&strm, 15, window);    // 0 -> 2
    inflateBackEnd(&strm);                 // 2 -> 0
}

int main() {
    variant_inflate_reset_cycle();
    variant_inflate_dict_sync_reset();
    variant_inflate_copy_and_diverge();
    variant_inflate_queries();
    variant_inflate_back_cycle();
    variant_inflate_reinit_cycles();
    return 0;
}
