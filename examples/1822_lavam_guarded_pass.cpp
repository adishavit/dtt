// 1822_lavam_guarded_pass.cpp
// LAVA-M pattern with proper bounds checking — should NOT trigger taint warnings.
// Tests that Z3 taint guards correctly suppress when bounds are proven.

extern "C" {
    void *malloc(unsigned long);
    void *memcpy(void*, const void*, unsigned long);
    void free(void*);
    long read(int, void*, unsigned long);
}

typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

// Variant 1: memcpy with proven upper bound (fixed Heartbleed)
void lavam_fixed_memcpy(int fd) {
    uint8_t packet[4096];
    read(fd, packet, sizeof(packet));

    unsigned int payload = ((unsigned int)packet[0] << 8) | packet[1];

    // Proper bounds check: payload bounded by known constant
    if (payload <= 128) {
        uint8_t response[256];
        memcpy(response, packet + 2, payload);   // Safe: payload <= 128
    }
}

// Variant 2: malloc with validated size
void lavam_validated_alloc(int fd) {
    uint32_t requested_size;
    read(fd, &requested_size, sizeof(requested_size));

    if (requested_size <= 65536) {
        void *p = malloc(requested_size);         // Safe: bounded by 64K
        if (p) free(p);
    }
}

// Variant 3: memcpy bounded by constant check
void lavam_record_bounded(int fd) {
    uint8_t packet[4096];

    read(fd, packet, 1024);

    unsigned int payload = ((unsigned int)packet[0] << 8) | packet[1];

    if (payload <= 1021) {
        uint8_t dst[1024];
        memcpy(dst, packet + 3, payload);  // Safe: payload <= 1021 < 1024
    }
}

int main() { return 0; }
