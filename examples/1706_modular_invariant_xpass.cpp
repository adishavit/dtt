// 1706: Modular/wrapping invariants - DTT false positive
// DTT does not model modular arithmetic invariants. Index variables maintained
// via modulo or bitmask are always in-bounds, but DTT cannot prove it.
// Found by scanning libwebp (src/utils/random_utils.h:47) and
// giflib (lib/gif_hash.c:73,96).

#include <stdint.h>
#include <string.h>

// Variant 1: modulo-wrapped index (libwebp VP8LRandom pattern)
#define TABLE_SIZE 55

struct Random {
    int index1;
    int index2;
    int tab[TABLE_SIZE];
};

int variant1_modulo_index(Random* r) {
    r->index1 = (r->index1 + 1) % TABLE_SIZE;
    r->index2 = (r->index2 + 1) % TABLE_SIZE;
    return r->tab[r->index1] - r->tab[r->index2];  // always in-bounds
}

// Variant 2: bitmask-bounded index (giflib gif_hash.c pattern)
#define HT_SIZE 8192
#define HT_KEY_MASK 0x1FFF

int variant2_bitmask_index() {
    int HTable[HT_SIZE];
    memset(HTable, 0, sizeof(HTable));

    uint32_t key = 123456789;
    int idx = (int)(key & HT_KEY_MASK);
    return HTable[idx];  // DTT FP: idx is always < 8192
}

// Variant 3: power-of-two modulo (ring buffer pattern)
#define RING_SIZE 256

int variant3_ring_buffer() {
    int ring[RING_SIZE] = {0};
    int head = 0;
    for (int i = 0; i < 1000; i++) {
        ring[head] = i;        // always in-bounds
        head = (head + 1) % RING_SIZE;
    }
    return ring[head];
}

// Variant 4: bitmask on computed hash
#define CACHE_BITS 10
#define CACHE_SIZE (1 << CACHE_BITS)
#define CACHE_MASK (CACHE_SIZE - 1)

int variant4_hash_mask() {
    int cache[CACHE_SIZE];
    memset(cache, 0, sizeof(cache));

    uint32_t hash = 0xDEADBEEF;
    int slot = (int)(hash & CACHE_MASK);
    return cache[slot];  // DTT FP: slot is always 0..1023
}

// Variant 5: modulo with variable divisor (bounded by array size)
int variant5_variable_modulo() {
    int buckets[16] = {0};
    int n_buckets = 16;
    for (int i = 0; i < 100; i++) {
        int idx = i % n_buckets;
        buckets[idx] += 1;  // always in-bounds when n_buckets <= 16
    }
    return buckets[0];
}

int main() {
    Random r;
    memset(&r, 0, sizeof(r));
    r.index1 = 0;
    r.index2 = 1;

    return variant1_modulo_index(&r)
         + variant2_bitmask_index()
         + variant3_ring_buffer()
         + variant4_hash_mask()
         + variant5_variable_modulo();
}
