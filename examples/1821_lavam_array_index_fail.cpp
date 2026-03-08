// 1821_lavam_array_index_fail.cpp
// LAVA-M benchmark pattern: tainted value used as array index (CWE-119/CWE-787)
// DTT catches these via bounds checking (_size_bytes tracking on stack arrays).
// The index variable has unknown value, so DTT correctly reports potential OOB.

extern "C" {
    long read(int, void*, unsigned long);
}

typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

// Variant 1: Tainted array index (direct)
void lavam_array_index(int fd) {
    uint32_t idx;
    read(fd, &idx, sizeof(idx));         // idx tainted

    char table[256];
    char val = table[idx];               // BUG: OOB read with tainted index
    (void)val;
}

// Variant 2: Tainted pointer arithmetic
void lavam_ptr_arith(int fd) {
    uint32_t offset;
    read(fd, &offset, sizeof(offset));   // offset tainted

    uint8_t buf[1024];
    uint8_t *p = buf + offset;           // tainted pointer
    *p = 0x41;                           // BUG: OOB write via tainted offset
}

// Variant 3: Magic-guarded tainted index (classic LAVA-M)
void lavam_magic_index(int fd) {
    uint32_t lava_val;
    read(fd, &lava_val, sizeof(lava_val));

    if (lava_val == 0x6c617661) {        // magic guard
        int data[100];
        data[lava_val] = 42;             // BUG: massive OOB (0x6c617661 >> 100)
    }
}

// Variant 4: Tainted value from global (lava_get pattern)
static uint32_t lava_storage;

void lavam_global_index(int fd) {
    uint8_t input[4096];
    read(fd, input, 4096);

    // lava_set equivalent
    lava_storage = *(uint32_t*)(input + 100);

    // lava_get equivalent, at attack point
    uint32_t idx = lava_storage;         // tainted
    char lookup[64];
    char result = lookup[idx];           // BUG: OOB with tainted index
    (void)result;
}

int main() { return 0; }
