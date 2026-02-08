// 1704: Unchecked pointer parameters - DTT false positive
// DTT assumes all pointer parameters could be null and requires null checks
// before dereference. Internal/static functions have implicit non-null contracts
// enforced by their callers, making these warnings false positives.
// Found by scanning libwebp (~700 FPs) and giflib (~60 FPs).

#include <stdlib.h>
#include <stdint.h>

struct Decoder {
    int width, height;
    uint8_t* pixels;
    int stride;
};

struct MBInfo {
    int x_offset, y_offset;
    int coeffs[16];
};

// Variant 1: static helper dereferences struct param (libwebp ProcessRow pattern)
static int process_row(Decoder* dec) {
    int w = dec->width;   // DTT FP: dec could be null
    int h = dec->height;  // DTT FP: dec could be null
    return w * h;
}

// Variant 2: static helper with two pointer params
static void copy_block(const MBInfo* src, MBInfo* dst) {
    dst->x_offset = src->x_offset;  // DTT FP: src/dst could be null
    dst->y_offset = src->y_offset;
}

// Variant 3: chained member access through pointer param
static int get_pixel(Decoder* dec, int x, int y) {
    return dec->pixels[y * dec->stride + x];  // DTT FP: dec, dec->pixels null
}

// Variant 4: pointer param used in loop
static int sum_coeffs(const MBInfo* mb, int count) {
    int sum = 0;
    for (int i = 0; i < count; i++)
        sum += mb->coeffs[i];  // DTT FP: mb could be null
    return sum;
}

// Variant 5: pointer param passed to another static function
static int decode_row(Decoder* dec, int row) {
    MBInfo mb;
    mb.x_offset = 0;
    mb.y_offset = row;
    for (int i = 0; i < 16; i++)
        mb.coeffs[i] = get_pixel(dec, i, row);  // dec validated by caller
    return sum_coeffs(&mb, 16);
}

int main() {
    Decoder dec;
    dec.width = 4;
    dec.height = 4;
    dec.stride = 4;
    uint8_t px[16] = {0};
    dec.pixels = px;

    MBInfo a = {1, 2, {0}};
    MBInfo b;
    copy_block(&a, &b);

    return process_row(&dec)
         + decode_row(&dec, 0)
         + sum_coeffs(&a, 16)
         + b.x_offset;
}
