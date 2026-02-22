// 1704: Unchecked pointer parameters — static function null deferral works
// DTT defers null-check warnings for internal/static function pointer parameters
// to their call sites. When all call sites pass non-null (e.g., &local_var),
// no warnings are emitted.

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

// Variant 1: static helper dereferences struct param
static int process_row(Decoder* dec) {
    int w = dec->width;
    int h = dec->height;
    return w * h;
}

// Variant 2: static helper with two pointer params
static void copy_block(const MBInfo* src, MBInfo* dst) {
    dst->x_offset = src->x_offset;
    dst->y_offset = src->y_offset;
}

// Variant 3: chained member access through pointer param
static int get_pixel(Decoder* dec, int x, int y) {
    return dec->pixels[y * dec->stride + x];
}

// Variant 4: pointer param passed to another static function
static int decode_row(Decoder* dec, int row) {
    MBInfo mb;
    mb.x_offset = 0;
    mb.y_offset = row;
    for (int i = 0; i < 16; i++)
        mb.coeffs[i] = get_pixel(dec, i, row);
    return mb.coeffs[0];
}

// Variant 5: pointer param with constant loop bound
static int sum_fixed(const MBInfo* mb) {
    int sum = 0;
    for (int i = 0; i < 16; i++)
        sum += mb->coeffs[i];
    return sum;
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
         + sum_fixed(&a)
         + b.x_offset;
}
