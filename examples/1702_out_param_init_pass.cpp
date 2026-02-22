// 1702: Out-parameter initialization — safe patterns
// When &local_var is passed to a non-const T* parameter, DTT assumes the callee
// writes through the pointer (out-parameter heuristic). No false positives expected.
// Found by scanning libwebp (src/enc/backward_references_cost_enc.c:108,
// src/dec/io_dec.c:198) and giflib (lib/dgif_lib.c).

#include <stdlib.h>
#include <stdint.h>

// Variant 1: simple out-parameter via pointer (libwebp VP8LPrefixEncodeBits pattern)
static void encode_bits(int value, int* code, int* extra_bits) {
    *code = value >> 4;
    *extra_bits = value & 0xF;
}

int variant1_simple_out_param() {
    int code, extra_bits;
    encode_bits(42, &code, &extra_bits);
    return code + extra_bits;  // DTT FP: code and extra_bits initialized by callee
}

// Variant 2: out-parameter with return value (libwebp GetAlphaSourceRow pattern)
static const uint8_t* get_row(const uint8_t* data, int* num_rows) {
    *num_rows = 4;
    return data + 16;
}

int variant2_out_param_with_retval() {
    uint8_t buf[64] = {0};
    int num_rows;
    const uint8_t* row = get_row(buf, &num_rows);
    (void)row;
    return num_rows;  // DTT FP: num_rows initialized by get_row
}

// Variant 3: multiple out-parameters
static void decompose(int pixel, uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = (pixel >> 16) & 0xFF;
    *g = (pixel >> 8) & 0xFF;
    *b = pixel & 0xFF;
}

int variant3_multiple_out_params() {
    uint8_t r, g, b;
    decompose(0xFF8040, &r, &g, &b);
    return r + g + b;  // DTT FP: all three initialized by decompose
}

// Variant 4: out-parameter initializing a struct field
struct Point { int x, y; };

static void get_origin(Point* out) {
    out->x = 0;
    out->y = 0;
}

int variant4_struct_out_param() {
    Point p;
    get_origin(&p);
    return p.x + p.y;  // DTT FP: p initialized by get_origin
}

// Variant 5: out-parameter via array pointer (giflib pattern)
static int read_data(uint8_t* buf, int max_len) {
    for (int i = 0; i < max_len; i++)
        buf[i] = (uint8_t)i;
    return max_len;
}

int variant5_array_out_param() {
    uint8_t buffer[16];
    int n = read_data(buffer, 16);
    return buffer[0] + n;  // DTT FP: buffer filled by read_data
}

int main() {
    return variant1_simple_out_param()
         + variant2_out_param_with_retval()
         + variant3_multiple_out_params()
         + variant4_struct_out_param()
         + variant5_array_out_param();
}
