// 1707: Cross-function bounds validation - DTT false positive
// When a value is validated in a caller and used in a callee, DTT does not
// propagate the constraint across the call boundary. The callee sees an
// unconstrained parameter and flags safe operations as potentially unsafe.
// Found by scanning libwebp (src/dec/vp8l_dec.c:457).

#include <stdint.h>
#include <string.h>

// Variant 1: caller validates shift amount, callee shifts
// (libwebp VP8LColorCacheInit pattern)
#define MAX_BITS 11

static int init_cache(int bits) {
    int size = 1 << bits;  // DTT FP: bits could be negative or >= 32
    return size;
}

int variant1_validated_shift() {
    int bits = 8;
    if (bits > 0 && bits <= MAX_BITS) {
        return init_cache(bits);
    }
    return 0;
}

// Variant 2: caller bounds-checks index, callee uses it
static int table_lookup(const int* table, int index, int table_size) {
    (void)table_size;
    return table[index];  // DTT FP: index could be out of bounds
}

int variant2_bounded_lookup() {
    int table[256] = {0};
    int idx = 100;
    if (idx >= 0 && idx < 256)
        return table_lookup(table, idx, 256);
    return -1;
}

// Variant 3: validated size passed to fill function
static void fill(uint8_t* buf, int n) {
    for (int i = 0; i < n; i++)
        buf[i] = (uint8_t)i;  // DTT FP: n could exceed buf capacity
}

int variant3_validated_fill() {
    uint8_t buffer[64];
    int len = 32;
    if (len > 0 && len <= 64)
        fill(buffer, len);
    return buffer[0];
}

// Variant 4: enum-like value validated then dispatched
static int process_mode(int mode) {
    static const int costs[] = {1, 2, 4, 8};
    return costs[mode];  // DTT FP: mode could be out of bounds
}

int variant4_validated_enum() {
    int mode = 2;
    if (mode >= 0 && mode < 4)
        return process_mode(mode);
    return -1;
}

// Variant 5: multi-level: caller → wrapper → inner
static int inner(const int* data, int width) {
    int sum = 0;
    for (int i = 0; i < width; i++)
        sum += data[i];  // DTT FP: width could exceed data bounds
    return sum;
}

static int wrapper(const int* data, int width, int max) {
    if (width > max) width = max;
    return inner(data, width);
}

int variant5_multi_level() {
    int data[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    return wrapper(data, 8, 16);
}

int main() {
    return variant1_validated_shift()
         + variant2_bounded_lookup()
         + variant3_validated_fill()
         + variant4_validated_enum()
         + variant5_multi_level();
}
