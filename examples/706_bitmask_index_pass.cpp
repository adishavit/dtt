// 706_bitmask_index_pass.cpp
// Tests for bitmask/modulo index patterns that are always safe
// DTT should recognize these patterns and NOT warn (false positive reduction)

#include <cstddef>
#include <cstdint>

// ============================================================================
// BITMASK PATTERNS (x & (N-1) for power-of-2 N)
// ============================================================================

// Variant 1: Simple bitmask - & 3 for array of 4
void variant1_and_3() {
    int arr[4] = {0, 1, 2, 3};
    for (int i = 0; i < 100; i++) {
        arr[i & 3] = i;  // SAFE: i & 3 is always 0-3
    }
}

// Variant 2: Bitmask - & 7 for array of 8
void variant2_and_7() {
    char buf[8] = {0};
    for (size_t i = 0; i < 1000; i++) {
        buf[i & 7] = (char)i;  // SAFE: i & 7 is always 0-7
    }
}

// Variant 3: Bitmask - & 15 for array of 16
void variant3_and_15() {
    int ring[16] = {0};
    int head = 0;
    for (int i = 0; i < 50; i++) {
        ring[head & 15] = i;  // SAFE: head & 15 is always 0-15
        head++;
    }
}

// Variant 4: Bitmask - & 0xFF for array of 256
void variant4_and_ff() {
    uint8_t lookup[256] = {0};
    for (int i = 0; i < 1000; i++) {
        int val = lookup[i & 0xFF];  // SAFE: i & 0xFF is always 0-255
        (void)val;
    }
}

// Variant 5: Bitmask - & (SIZE-1) with constant
constexpr size_t RING_SIZE = 32;
void variant5_and_size_minus_1() {
    int ring[RING_SIZE] = {0};
    for (size_t i = 0; i < 100; i++) {
        ring[i & (RING_SIZE - 1)] = (int)i;  // SAFE: always 0 to 31
    }
}

// Variant 6: Bitmask in ring buffer read/write
void variant6_ring_buffer_rw() {
    char ring[64] = {0};
    size_t read_idx = 0;
    size_t write_idx = 0;

    for (int i = 0; i < 100; i++) {
        ring[write_idx & 63] = (char)i;  // SAFE: & 63
        write_idx++;

        char c = ring[read_idx & 63];    // SAFE: & 63
        read_idx++;
        (void)c;
    }
}

// ============================================================================
// MODULO PATTERNS (x % N)
// ============================================================================

// Variant 7: Simple modulo - % 4
void variant7_mod_4() {
    int arr[4] = {0, 1, 2, 3};
    for (int i = 0; i < 100; i++) {
        arr[i % 4] = i;  // SAFE: i % 4 is always 0-3
    }
}

// Variant 8: Modulo - % 10
void variant8_mod_10() {
    int digits[10] = {0};
    for (int i = 0; i < 1000; i++) {
        digits[i % 10]++;  // SAFE: i % 10 is always 0-9
    }
}

// Variant 9: Modulo with size constant
constexpr int TABLE_SIZE = 100;
void variant9_mod_constant() {
    int table[TABLE_SIZE] = {0};
    for (int key = 0; key < 10000; key++) {
        int idx = key % TABLE_SIZE;  // SAFE: always 0 to 99
        table[idx]++;
    }
}

// Variant 10: Modulo for circular buffer
void variant10_circular_mod() {
    double buffer[50] = {0};
    int pos = 0;
    for (int i = 0; i < 200; i++) {
        buffer[pos % 50] = (double)i;  // SAFE: pos % 50 is always 0-49
        pos++;
    }
}

// ============================================================================
// COMBINED PATTERNS
// ============================================================================

// Variant 11: Bitmask after arithmetic
void variant11_arithmetic_then_mask() {
    int arr[8] = {0};
    int a = 1000;
    int b = 500;
    int idx = (a + b) & 7;  // SAFE: result is always 0-7
    arr[idx] = 42;
}

// Variant 12: Bitmask on shifted value
void variant12_shift_then_mask() {
    char arr[16] = {0};
    uint32_t val = 0xDEADBEEF;
    arr[(val >> 4) & 15] = 'x';   // SAFE: & 15 guarantees 0-15
    arr[(val >> 8) & 15] = 'y';   // SAFE
    arr[(val >> 12) & 15] = 'z';  // SAFE
}

// Variant 13: Multiple masks in expression
void variant13_multiple_masks() {
    int arr[4] = {0};
    for (int i = 0; i < 100; i++) {
        int j = i * 7;
        arr[(j & 3) + (0 & 3)] = i;  // SAFE: both masked to 0-3, sum is 0-6
                                      // Wait, this could be 0-6 for array of 4!
                                      // Let me fix this...
    }
}

// Variant 13 corrected: Safe double mask
void variant13_corrected_double_mask() {
    int arr[4] = {0};
    for (int i = 0; i < 100; i++) {
        // Both operands masked, then result masked
        int idx = ((i & 3) + (i & 3)) & 3;  // SAFE: final & 3 guarantees 0-3
        arr[idx] = i;
    }
}

// Variant 14: Bitmask in hash table
struct Entry {
    int key;
    int value;
};

constexpr size_t HASH_SIZE = 128;  // Power of 2

void variant14_hash_table_mask() {
    Entry table[HASH_SIZE] = {{0, 0}};

    for (int key = 0; key < 1000; key++) {
        size_t hash = (size_t)key * 2654435761u;  // Simple hash
        size_t idx = hash & (HASH_SIZE - 1);      // SAFE: always 0 to 127
        table[idx].key = key;
        table[idx].value = key * 2;
    }
}

// ============================================================================
// EDGE CASES
// ============================================================================

// Variant 15: Bitmask with 0 (degenerate case)
void variant15_and_0() {
    int arr[1] = {0};
    for (int i = 0; i < 100; i++) {
        // i & 0 is always 0, so this always accesses arr[0]
        arr[i & 0] = i;  // SAFE but unusual
    }
}

// Variant 16: Bitmask equals array size minus 1
void variant16_exact_fit() {
    int arr[256] = {0};
    for (int i = 0; i < 10000; i++) {
        arr[i & 255] = i;  // SAFE: 255 = 256-1, perfect fit
    }
}

// Variant 17: Modulo with signed integers (potential issue)
void variant17_signed_mod() {
    int arr[10] = {0};
    for (int i = -50; i < 50; i++) {
        // Note: In C++, negative % positive can be negative!
        // But abs(i % 10) would be safe
        int idx = ((i % 10) + 10) % 10;  // SAFE: handles negative correctly
        arr[idx] = i;
    }
}

// Variant 18: Unsigned modulo (always safe for positive)
void variant18_unsigned_mod() {
    int arr[10] = {0};
    for (unsigned int i = 0; i < 1000; i++) {
        arr[i % 10] = (int)i;  // SAFE: unsigned % always non-negative
    }
}

// Variant 19: Power-of-2 detection and masking
template<size_t N>
void variant19_template_mask() {
    static_assert((N & (N-1)) == 0, "N must be power of 2");
    int arr[N] = {0};
    for (size_t i = 0; i < 100; i++) {
        arr[i & (N-1)] = (int)i;  // SAFE: N is power of 2
    }
}

void variant19_caller() {
    variant19_template_mask<8>();
    variant19_template_mask<16>();
    variant19_template_mask<32>();
}

// Variant 20: Complex expression with guaranteed bounds
void variant20_complex_bounded() {
    char arr[256] = {0};

    for (int i = 0; i < 100; i++) {
        // XOR, shift, then mask - final mask guarantees bounds
        uint8_t idx = ((i ^ 0x55) << 2) & 0xFF;
        // Wait, 0xFF allows 0-255 but array is 256, so it's safe
        // But << 2 could overflow uint8_t... let me fix
    }
}

// Variant 20 corrected
void variant20_corrected() {
    char arr[256] = {0};
    for (int i = 0; i < 100; i++) {
        // Cast to ensure proper width, then mask
        unsigned int temp = (unsigned int)(i ^ 0x55) << 2;
        uint8_t idx = temp & 0xFF;  // SAFE: & 0xFF guarantees 0-255
        arr[idx] = (char)i;
    }
}

int main() {
    // All these patterns should compile without DTT warnings
    // because the bitmask/modulo guarantees bounds
    variant1_and_3();
    variant2_and_7();
    variant3_and_15();
    variant7_mod_4();
    return 0;
}
