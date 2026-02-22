// 851_integer_overflow_size_fail.cpp
// Tests: Integer overflow in size calculations leading to buffer issues
// These often result in Heartbleed-like vulnerabilities through overflow

#include <cstring>
#include <cstdint>
#include <cstdlib>

// =============================================================================
// SECTION 1: Multiplication Overflow
// =============================================================================

// Variant 1.1: count * size overflow wraps to small value
void overflow_mul_memcpy(size_t count, size_t elem_size) {
    size_t total = count * elem_size;  // Can overflow!
    // If count=0x100000001, elem_size=16, total wraps to 16
    char* buf = (char*)malloc(total);
    if (buf) {
        // We think we have 'total' bytes, but allocation might be tiny
        memset(buf, 0, total);  // ERROR: total might have wrapped
        free(buf);
    }
}

// Variant 1.2: Array size calculation overflow
void overflow_array_size(uint32_t num_elements) {
    struct Element { char data[256]; };

    // This can wrap around
    size_t alloc_size = num_elements * sizeof(Element);

    char* buffer = (char*)malloc(alloc_size);
    if (buffer) {
        // Now we try to use as if we have num_elements * 256 bytes
        for (uint32_t i = 0; i < num_elements; i++) {
            memset(buffer + i * sizeof(Element), 0, sizeof(Element));  // ERROR
        }
        free(buffer);
    }
}

// Variant 1.3: Pixel buffer calculation
void overflow_pixel_buffer(uint32_t width, uint32_t height, uint32_t bpp) {
    // Classic image processing overflow
    size_t row_size = width * bpp;           // Overflow?
    size_t total_size = row_size * height;   // Double overflow?

    uint8_t* pixels = (uint8_t*)malloc(total_size);
    if (pixels) {
        memset(pixels, 0, total_size);  // ERROR: total_size might have wrapped
        free(pixels);
    }
}

// =============================================================================
// SECTION 2: Addition Overflow
// =============================================================================

// Variant 2.1: header_size + payload_size overflow
void overflow_add_sizes(size_t header_size, size_t payload_size) {
    size_t total = header_size + payload_size;  // Can wrap to small value

    char* buf = (char*)malloc(total);
    if (buf) {
        memset(buf, 0, header_size);
        memset(buf + header_size, 'A', payload_size);  // ERROR: if wrapped
        free(buf);
    }
}

// Variant 2.2: offset + length overflow check bypass
void overflow_add_check_bypass(const uint8_t* data, size_t data_len,
                               size_t offset, size_t length) {
    // Attacker sets offset=0xFFFFFFFF, length=0x100
    // offset + length wraps to 0xFF, passes check!
    if (offset + length <= data_len) {
        uint8_t local[256];
        if (length <= sizeof(local)) {
            memcpy(local, data + offset, length);  // ERROR: offset was huge!
        }
    }
}

// Variant 2.3: String concatenation size
void overflow_strcat_size(const char* s1, size_t s1_len,
                          const char* s2, size_t s2_len) {
    size_t total = s1_len + s2_len + 1;  // +1 for null, can still wrap

    char* result = (char*)malloc(total);
    if (result) {
        memcpy(result, s1, s1_len);
        memcpy(result + s1_len, s2, s2_len);  // ERROR: if total wrapped
        result[s1_len + s2_len] = '\0';
        free(result);
    }
}

// =============================================================================
// SECTION 3: Subtraction Underflow
// =============================================================================

// Variant 3.1: remaining = total - used underflow
void underflow_remaining(size_t total_size, size_t used) {
    // If used > total_size, this wraps to huge value
    size_t remaining = total_size - used;

    char buf[1024];
    if (remaining <= sizeof(buf)) {  // Huge value fails, but...
        // ... what if attacker makes it wrap to small value?
    }

    // More dangerous:
    char* dst = buf;
    memset(dst, 0, remaining);  // ERROR: remaining might have underflowed
}

// Variant 3.2: Available space calculation
void underflow_available(uint8_t* buffer, size_t buffer_size,
                         size_t header_len, size_t payload_len) {
    // Assume header was written, calculate remaining
    size_t available = buffer_size - header_len;  // Underflow if header_len > buffer_size

    if (payload_len <= available) {
        memcpy(buffer + header_len, "payload", payload_len);  // ERROR
    }
}

// =============================================================================
// SECTION 4: Type Truncation
// =============================================================================

// Variant 4.1: 64-bit to 32-bit truncation
void truncation_64_to_32(size_t requested_size) {
    uint32_t alloc_size = static_cast<uint32_t>(requested_size);  // Truncates!

    // requested_size = 0x100000100 becomes alloc_size = 0x100
    char* buf = (char*)malloc(alloc_size);
    if (buf) {
        memset(buf, 0, requested_size);  // ERROR: using original huge size!
        free(buf);
    }
}

// Variant 4.2: size_t to uint16_t truncation
void truncation_to_16bit(const uint8_t* src, size_t src_size,
                         uint8_t* dst, uint16_t copy_len) {
    // If caller passes copy_len that was truncated from larger value
    memcpy(dst, src, copy_len);  // ERROR: copy_len might have been truncated
}

// Variant 4.3: Signed/unsigned mismatch
void signed_unsigned_mismatch(const char* data, int data_len) {
    if (data_len < 0) return;  // Try to catch negative

    char buf[256];
    // But data_len could be INT_MAX, and we might still overflow
    if (data_len < 256) {
        memcpy(buf, data, data_len);  // Might be OK here but...
    }

    // More problematic:
    size_t size = data_len + 100;  // If data_len was negative...
    (void)size;
}

// =============================================================================
// SECTION 5: Signedness Vulnerabilities
// =============================================================================

// Variant 5.1: Signed comparison then unsigned use
void signed_size_check(const uint8_t* data, int length) {
    uint8_t buf[100];

    // Attacker sends length = -1
    if (length < 100) {  // -1 < 100, passes!
        memcpy(buf, data, length);  // ERROR: length becomes SIZE_MAX
    }
}

// Variant 5.2: Negative offset becomes huge positive
void signed_offset(const uint8_t* base, int offset, size_t len) {
    // offset = -1 becomes 0xFFFFFFFF when converted to size_t
    memcpy((void*)base, base + offset, len);  // ERROR: reads from wrong location
}

// =============================================================================
// SECTION 6: Allocation Size Overflow
// =============================================================================

// Variant 6.1: CVE-style allocation overflow
void* vulnerable_alloc(uint32_t count, uint32_t size) {
    // No overflow check!
    uint32_t total = count * size;
    void* p = malloc(total);
    if (p) {
        // Attacker made total wrap to small value
        // Now we think we have count*size bytes but only have 'total' bytes
        memset(p, 0, count * size);  // ERROR: might exceed allocation
    }
    return p;
}

// Variant 6.2: calloc-like without proper check
void* broken_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;  // No overflow check!
    void* p = malloc(total);
    if (p) {
        memset(p, 0, total);  // If overflow, might be less than expected
    }
    return p;
}

// =============================================================================
// SECTION 7: Protocol Length Field Overflow
// =============================================================================

struct NetworkHeader {
    uint16_t msg_type;
    uint16_t num_items;
    uint16_t item_size;
};

// Variant 7.1: num_items * item_size overflow
void overflow_protocol_length(const uint8_t* packet, size_t packet_len) {
    const NetworkHeader* hdr = reinterpret_cast<const NetworkHeader*>(packet);

    // This can overflow if num_items and item_size are large
    size_t data_size = static_cast<size_t>(hdr->num_items) * hdr->item_size;

    uint8_t local[4096];
    if (data_size <= sizeof(local)) {
        // data_size wrapped to small value, passes check
        // But we then process num_items * item_size which is huge
        const uint8_t* data = packet + sizeof(NetworkHeader);
        for (uint16_t i = 0; i < hdr->num_items; i++) {
            memcpy(local + i * hdr->item_size, data + i * hdr->item_size,
                   hdr->item_size);  // ERROR: reads beyond packet
        }
    }
}

// =============================================================================
// SECTION 8: Loop Counter Overflow
// =============================================================================

// Variant 8.1: Counter wraps to zero, loop runs forever
void overflow_loop_counter(const uint8_t* data, uint8_t length) {
    uint8_t buf[256];
    uint8_t i = 0;

    // If length is 0, and we use post-decrement...
    while (length-- > 0) {  // Unsigned underflow makes this loop forever!
        buf[i++] = data[i];  // ERROR: will overflow buf
    }
}

// Variant 8.2: Size calculation in loop bound
void overflow_loop_bound(const uint8_t* items, uint16_t count, uint16_t size) {
    uint8_t local[1024];
    size_t offset = 0;

    for (uint16_t i = 0; i < count; i++) {
        // offset += size can overflow
        if (offset + size <= sizeof(local)) {
            memcpy(local + offset, items + offset, size);  // ERROR if overflow
        }
        offset += size;
    }
}

// =============================================================================
// SECTION 9: Pointer Arithmetic Overflow
// =============================================================================

// Variant 9.1: Adding huge offset to pointer
void overflow_ptr_add(uint8_t* base, size_t offset) {
    uint8_t* p = base + offset;  // If offset is huge, p might wrap!
    // p could now point before base or to completely different memory
    *p = 0;  // ERROR: undefined if pointer wrapped
}

// Variant 9.2: Array indexing with overflowed index
void overflow_array_index(uint32_t* arr, uint64_t index) {
    // If sizeof(uint32_t) * index overflows...
    arr[index] = 0;  // ERROR: might access wrong memory
}

// =============================================================================
// SECTION 10: Compound Expression Overflow
// =============================================================================

// Variant 10.1: Multiple operations chain
void overflow_compound(uint32_t a, uint32_t b, uint32_t c) {
    // (a + b) * c has multiple overflow opportunities
    size_t size = (a + b) * c;

    char* buf = (char*)malloc(size);
    if (buf) {
        memset(buf, 0, size);  // ERROR: size might have overflowed
        free(buf);
    }
}

// Variant 10.2: Alignment calculation overflow
void overflow_alignment(size_t size, size_t alignment) {
    // Round up to alignment
    // (size + alignment - 1) & ~(alignment - 1)
    size_t aligned = (size + alignment - 1) & ~(alignment - 1);
    // The addition can overflow!

    void* p = malloc(aligned);
    if (p) {
        memset(p, 0, aligned);  // ERROR: aligned might have wrapped
        free(p);
    }
}

// =============================================================================
// SECTION 11: Fixed-Point/Fractional Overflow
// =============================================================================

// Variant 11.1: Percentage calculation overflow
void overflow_percentage(size_t value, uint32_t percent) {
    // (value * percent) / 100 - multiplication can overflow
    size_t result = (value * percent) / 100;

    char* buf = (char*)malloc(result);
    if (buf) {
        memset(buf, 0, result);  // ERROR: result from overflowed calc
        free(buf);
    }
}

// =============================================================================
// SECTION 12: CVE-Inspired Patterns
// =============================================================================

// Variant 12.1: CVE-2021-3156 style (sudo heap overflow)
// Simplified: backslash escape counting overflow
void overflow_escape_count(const char* src, size_t src_len) {
    size_t extra = 0;
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '\\') extra++;  // Count escapes
    }

    // Attacker controls src with many backslashes
    size_t alloc_size = src_len + extra + 1;  // Can overflow!

    char* dst = (char*)malloc(alloc_size);
    if (dst) {
        // Now write more than allocated
        size_t j = 0;
        for (size_t i = 0; i < src_len; i++) {
            if (src[i] == '\\') dst[j++] = '\\';  // Double escapes
            dst[j++] = src[i];  // ERROR: j can exceed alloc_size
        }
        free(dst);
    }
}

// Variant 12.2: CVE-2017-9022 style (image dimension overflow)
void overflow_image_dimensions(uint32_t width, uint32_t height) {
    uint32_t bytes_per_pixel = 4;
    uint32_t row_bytes = width * bytes_per_pixel;  // Overflow #1
    uint32_t total_bytes = row_bytes * height;     // Overflow #2

    uint8_t* image = (uint8_t*)malloc(total_bytes);
    if (image) {
        // Write assuming we have width * height * 4 bytes
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                // This index calculation can also overflow!
                size_t offset = y * row_bytes + x * bytes_per_pixel;
                image[offset] = 0xFF;  // ERROR: offset exceeds allocation
            }
        }
        free(image);
    }
}

int main() {
    return 0;
}
