// 1708: Negative signed index — patterns DTT correctly handles
// DTT should NOT flag these: each signed int index is provably non-negative
// from context (cast, loop, constant folding, arithmetic).

#include <stdint.h>

// Variant 1: distance computed from unsigned difference, cast to int
int variant1_unsigned_origin() {
    uint32_t current = 100;
    uint32_t base = 50;
    int offset = (int)(current - base);
    int table[64] = {0};
    if (offset < 64)
        return table[offset];  // Safe: unsigned origin, upper-bound guarded
    return 0;
}

// Variant 2: loop counter starts at 0
int variant2_loop_counter() {
    int data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int sum = 0;
    for (int i = 0; i < 10; i++)
        sum += data[i];  // Safe: i in [0,9]
    return sum;
}

// Variant 3: division of positive literal constants
int variant3_positive_division() {
    int table[32] = {0};
    int total = 100;
    int chunk = 4;
    int idx = total / chunk;  // = 25, non-negative
    if (idx < 32)
        return table[idx];  // Safe: 100/4 = 25
    return 0;
}

// Variant 4: modular index (bitmask)
int variant4_bitmask_index() {
    int table[32] = {0};
    int key = 12345;
    int idx = key & 0x1F;  // [0, 31]
    return table[idx];  // Safe: bitmask guarantees [0, 31]
}

// Variant 5: modular index (modulo)
int variant5_modulo_index() {
    int table[16] = {0};
    int hash = 9999;
    int idx = hash % 16;  // [0, 15] (for positive hash)
    return table[idx];  // Safe: mod guarantees upper bound
}

// Variant 6: explicit cast from unsigned with struct member
struct Record { uint32_t index; };

int variant6_unsigned_member_cast() {
    int table[256] = {0};
    Record r;
    r.index = 42;
    int idx = (int)(r.index & 0xFF);  // [0, 255]
    return table[idx];  // Safe: bitmask on unsigned, cast preserves bound
}

int main() {
    return variant1_unsigned_origin()
         + variant2_loop_counter()
         + variant3_positive_division()
         + variant4_bitmask_index()
         + variant5_modulo_index()
         + variant6_unsigned_member_cast();
}
