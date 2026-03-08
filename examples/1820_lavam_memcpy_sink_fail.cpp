// 1820_lavam_memcpy_sink_fail.cpp
// LAVA-M benchmark pattern: tainted input bytes flow to memcpy length (CWE-120)
// This is the Heartbleed-class pattern that LAVA-M heavily uses.
// DTT should catch this via the memcpy size sink.

extern "C" {
    void *malloc(unsigned long);
    void *memcpy(void*, const void*, unsigned long);
    void free(void*);
    long read(int, void*, unsigned long);
}

typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

// Variant 1: Direct read → memcpy length (simplest LAVA-M pattern)
// DUA bytes read from fd, used directly as memcpy length
void lavam_memcpy_direct(int fd) {
    uint8_t buf[4096];
    uint32_t lava_val;

    // Taint source: read from network/file
    read(fd, &lava_val, sizeof(lava_val));     // lava_val tainted

    // Attack point: tainted value as memcpy length
    uint8_t dst[256];
    memcpy(dst, buf, lava_val);                // ERROR: tainted copy length
}

// Variant 2: Magic value guard doesn't clear taint
// LAVA-M bugs are guarded by magic value comparisons
void lavam_magic_guard(int fd) {
    uint8_t buf[4096];
    uint32_t lava_val;

    read(fd, &lava_val, sizeof(lava_val));     // lava_val tainted

    // Magic value guard (0x6c617661 = "lava" in ASCII)
    // This comparison does NOT sanitize the value
    if (lava_val == 0x6c617661) {
        uint8_t dst[64];
        memcpy(dst, buf, lava_val);            // ERROR: still tainted
    }
}

// Variant 3: Taint through intermediate variable (lava_set/lava_get pattern)
// LAVA uses globals to transport DUA values to attack points
static uint32_t lava_global;

void lavam_global_transport(int fd) {
    uint8_t input[4096];
    read(fd, input, sizeof(input));            // input tainted

    // lava_set: copy DUA bytes to global
    memcpy(&lava_global, input + 42, sizeof(lava_global));

    // ... later, at attack point ...
    uint8_t response[128];
    memcpy(response, input, lava_global);      // ERROR: tainted copy length
}

// Variant 4: Taint through arithmetic (payload + padding pattern)
void lavam_arithmetic_propagation(int fd) {
    uint8_t packet[4096];
    read(fd, packet, sizeof(packet));          // packet tainted

    // Extract payload length from packet (like Heartbleed n2s macro)
    unsigned int payload = ((unsigned int)packet[0] << 8) | packet[1];
    unsigned int padding = 16;

    // Tainted arithmetic: payload + padding still tainted
    uint8_t *response = (uint8_t*)malloc(1 + 2 + payload + padding);  // ERROR: tainted alloc
    if (!response) return;

    // Copy with tainted length
    memcpy(response + 3, packet + 2, payload);  // ERROR: tainted copy length

    free(response);
}

int main() { return 0; }
