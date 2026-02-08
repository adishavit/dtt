// 1705: Bitfield width constraints - DTT false positive
// DTT does not model bitfield width constraints. A 2-bit field holds values
// 0-3, making it safe as an index into a [4] array, but DTT flags possible OOB
// because it cannot infer the range from the bitfield width.
// Found by scanning libwebp (src/enc/analysis_enc.c:50).

#include <stdint.h>

struct VP8MBInfo {
    unsigned int type    : 4;  // values 0-15
    unsigned int segment : 2;  // values 0-3
    unsigned int skip    : 1;  // values 0-1
};

// Variant 1: 2-bit segment from parameter (libwebp analysis_enc.c pattern)
static int get_quant(const VP8MBInfo* mb) {
    int quant[4] = {10, 20, 30, 40};
    return quant[mb->segment];  // DTT FP: segment is always 0-3 by bitfield width
}

// Variant 2: 1-bit field as boolean index
static int get_label(const VP8MBInfo* mb) {
    const int labels[2] = {0, 1};
    return labels[mb->skip];  // DTT FP: skip is always 0-1
}

// Variant 3: 4-bit type field indexing into [16] array
static int get_type_cost(const VP8MBInfo* mb) {
    int costs[16] = {0};
    return costs[mb->type];  // DTT FP: type is always 0-15
}

// Variant 4: bitfield set by external input then used as index
static int classify(int raw_segment) {
    VP8MBInfo mb;
    mb.segment = raw_segment;  // truncated to 2 bits by hardware
    int weights[4] = {1, 2, 4, 8};
    return weights[mb.segment];  // DTT FP: segment is 0-3 after truncation
}

// Variant 5: arithmetic on bitfield values
static int combined_index(const VP8MBInfo* mb) {
    int table[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    unsigned int idx = mb->segment * 2 + mb->skip;  // max = 3*2+1 = 7
    return table[idx];  // DTT FP: idx bounded by bitfield widths
}

int main() {
    VP8MBInfo mb;
    mb.type = 3;
    mb.segment = 2;
    mb.skip = 1;

    return get_quant(&mb)
         + get_label(&mb)
         + get_type_cost(&mb)
         + classify(99)
         + combined_index(&mb);
}
