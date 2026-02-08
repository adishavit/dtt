// 1708: Negative signed index - DTT false positive
// DTT flags signed integer array indices as potentially negative even when the
// value is constrained to be non-negative by context (upper bound check,
// function semantics, or domain invariant).
// Found by scanning libwebp (src/dsp/lossless_common.h:149).

#include <stdint.h>

// Variant 1: upper-bound checked but no explicit lower-bound check
// (libwebp DistanceToPlaneCode pattern)
#define DISTANCE_TABLE_SIZE 120

static const int kDistanceTable[DISTANCE_TABLE_SIZE] = {0};

int variant1_upper_bound_only(int distance) {
    if (distance < DISTANCE_TABLE_SIZE)
        return kDistanceTable[distance];  // DTT FP: distance could be negative
    return -1;
}

// Variant 2: distance computed from unsigned difference
int variant2_unsigned_origin() {
    uint32_t current = 100;
    uint32_t base = 50;
    int offset = (int)(current - base);  // always >= 0 when current >= base
    int table[64] = {0};
    if (offset < 64)
        return table[offset];  // DTT FP: offset could be negative
    return 0;
}

// Variant 3: loop counter as index (always non-negative)
int variant3_loop_counter() {
    int data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int sum = 0;
    for (int i = 0; i < 10; i++)
        sum += data[i];  // DTT FP: i could be negative (but loop starts at 0)
    return sum;
}

// Variant 4: return value of strlen-like function (always >= 0)
static int my_strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

int variant4_strlen_result() {
    const char* msg = "hello";
    int len = my_strlen(msg);
    char buf[10] = {0};
    if (len < 10)
        return buf[len];  // DTT FP: len could be negative
    return 0;
}

// Variant 5: division result (dividend and divisor both positive)
int variant5_positive_division() {
    int table[32] = {0};
    int total = 100;
    int chunk = 4;
    int idx = total / chunk;  // = 25, always >= 0 for positive operands
    if (idx < 32)
        return table[idx];  // DTT FP: idx could be negative
    return 0;
}

int main() {
    return variant1_upper_bound_only(50)
         + variant2_unsigned_origin()
         + variant3_loop_counter()
         + variant4_strlen_result()
         + variant5_positive_division();
}
