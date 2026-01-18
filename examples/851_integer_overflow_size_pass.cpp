// 851_integer_overflow_size_pass.cpp
// Tests: Safe patterns that PREVENT integer overflow in size calculations

#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <limits>

// =============================================================================
// SECTION 1: Multiplication Overflow Checks
// =============================================================================

// Variant 1.1: Check before multiply
void* safe_mul_alloc(size_t count, size_t elem_size) {
    // Check for overflow before multiplication
    if (count > 0 && elem_size > SIZE_MAX / count) {
        return nullptr;  // Would overflow
    }

    size_t total = count * elem_size;
    void* p = malloc(total);
    if (p) {
        memset(p, 0, total);  // OK: no overflow
    }
    return p;
}

// Variant 1.2: Use compiler builtin
void* safe_mul_builtin(size_t count, size_t size) {
    size_t total;
#if __has_builtin(__builtin_mul_overflow)
    if (__builtin_mul_overflow(count, size, &total)) {
        return nullptr;
    }
#else
    if (count > 0 && size > SIZE_MAX / count) return nullptr;
    total = count * size;
#endif
    void* p = malloc(total);
    if (p) memset(p, 0, total);  // OK
    return p;
}

// Variant 1.3: Image dimensions with overflow check
bool safe_image_alloc(uint32_t width, uint32_t height, uint32_t bpp,
                      uint8_t** out_pixels) {
    if (!out_pixels) return false;  // Null check

    // Check each multiplication step
    if (bpp == 0 || width > UINT32_MAX / bpp) return false;
    uint32_t row_bytes = width * bpp;

    if (height == 0 || row_bytes > SIZE_MAX / height) return false;
    size_t total = static_cast<size_t>(row_bytes) * height;

    *out_pixels = (uint8_t*)malloc(total);
    if (*out_pixels) {
        memset(*out_pixels, 0, total);  // OK: validated
        return true;
    }
    return false;
}

// =============================================================================
// SECTION 2: Addition Overflow Checks
// =============================================================================

// Variant 2.1: Check before add
void* safe_add_alloc(size_t header_size, size_t payload_size) {
    if (payload_size > SIZE_MAX - header_size) {
        return nullptr;  // Would overflow
    }

    size_t total = header_size + payload_size;
    void* p = malloc(total);
    if (p) {
        memset(p, 0, total);  // OK
    }
    return p;
}

// Variant 2.2: Offset + length check
bool safe_bounds_check(size_t buffer_len, size_t offset, size_t length) {
    // Check offset is valid
    if (offset > buffer_len) return false;

    // Check length doesn't overflow when added to offset
    if (length > buffer_len - offset) return false;

    return true;  // offset + length <= buffer_len, no overflow
}

void safe_copy_at_offset(const uint8_t* data, size_t data_len,
                         size_t offset, size_t length, uint8_t* out) {
    if (!safe_bounds_check(data_len, offset, length)) return;
    if (length > 256) return;  // output limit

    memcpy(out, data + offset, length);  // OK: validated
}

// =============================================================================
// SECTION 3: Subtraction Underflow Protection
// =============================================================================

// Variant 3.1: Check before subtract
size_t safe_remaining(size_t total, size_t used) {
    if (used > total) return 0;  // Prevent underflow
    return total - used;
}

// Variant 3.2: Saturating subtract
size_t saturating_sub(size_t a, size_t b) {
    return (a > b) ? (a - b) : 0;
}

void safe_with_saturating(uint8_t* buffer, size_t buffer_size,
                          size_t header_len, size_t payload_len) {
    size_t available = saturating_sub(buffer_size, header_len);

    if (payload_len <= available) {
        memcpy(buffer + header_len, "payload", payload_len);  // OK
    }
}

// =============================================================================
// SECTION 4: Type Conversion Safety
// =============================================================================

// Variant 4.1: Check range before narrowing
bool safe_narrow_to_32(size_t value, uint32_t* out) {
    if (!out) return false;  // Null check
    if (value > UINT32_MAX) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

// Variant 4.2: Use same type throughout
void safe_same_type(const uint8_t* src, size_t src_size,
                    uint8_t* dst, size_t dst_size,
                    size_t copy_len) {
    // All parameters are size_t, no truncation
    if (copy_len > src_size) return;
    if (copy_len > dst_size) return;

    memcpy(dst, src, copy_len);  // OK
}

// =============================================================================
// SECTION 5: Signed/Unsigned Safety
// =============================================================================

// Variant 5.1: Convert signed to unsigned safely
bool safe_signed_to_size(int length, size_t* out) {
    if (!out) return false;  // Null check
    if (length < 0) return false;
    *out = static_cast<size_t>(length);
    return true;
}

void safe_with_signed(const uint8_t* data, int length, uint8_t* buf) {
    size_t safe_len;
    if (!safe_signed_to_size(length, &safe_len)) return;

    if (safe_len > 256) return;

    memcpy(buf, data, safe_len);  // OK: validated
}

// Variant 5.2: Use unsigned types from the start
void safe_unsigned_api(const uint8_t* data, size_t length, uint8_t* buf, size_t buf_size) {
    // No signed/unsigned conversion needed
    if (length > buf_size) return;
    memcpy(buf, data, length);  // OK
}

// =============================================================================
// SECTION 6: Safe Allocation Patterns
// =============================================================================

// Variant 6.1: reallocarray-style (OpenBSD)
void* safe_reallocarray(void* ptr, size_t nmemb, size_t size) {
    if (nmemb > 0 && size > SIZE_MAX / nmemb) {
        return nullptr;  // Overflow
    }
    return realloc(ptr, nmemb * size);
}

// Variant 6.2: Checked calloc implementation
void* safe_calloc(size_t count, size_t size) {
    // Check for overflow
    if (count > 0 && size > SIZE_MAX / count) {
        return nullptr;
    }

    size_t total = count * size;
    void* p = malloc(total);
    if (p) {
        memset(p, 0, total);  // OK: no overflow
    }
    return p;
}

// =============================================================================
// SECTION 7: Protocol Length Validation
// =============================================================================

struct NetworkHeader {
    uint16_t msg_type;
    uint16_t num_items;
    uint16_t item_size;
};

// Variant 7.1: Validate product before use
void safe_protocol_parse(const uint8_t* packet, size_t packet_len) {
    if (!packet) return;  // Null check
    if (packet_len < sizeof(NetworkHeader)) return;

    const NetworkHeader* hdr = reinterpret_cast<const NetworkHeader*>(packet);

    // Check multiplication for overflow
    size_t num = hdr->num_items;
    size_t size = hdr->item_size;

    if (num > 0 && size > SIZE_MAX / num) return;  // Overflow
    size_t data_size = num * size;

    // Check against available data
    size_t available = packet_len - sizeof(NetworkHeader);
    if (data_size > available) return;

    uint8_t local[4096];
    if (data_size <= sizeof(local)) {
        memcpy(local, packet + sizeof(NetworkHeader), data_size);  // OK
    }
}

// =============================================================================
// SECTION 8: Loop Counter Safety
// =============================================================================

// Variant 8.1: Check bounds before loop
void safe_loop(const uint8_t* data, size_t data_len, size_t item_size) {
    if (item_size == 0) return;

    size_t max_items = data_len / item_size;  // Safe division

    uint8_t local[1024];
    for (size_t i = 0; i < max_items && i * item_size < sizeof(local); i++) {
        memcpy(local + i * item_size, data + i * item_size, item_size);  // OK
    }
}

// =============================================================================
// SECTION 9: Alignment Calculation Safety
// =============================================================================

// Variant 9.1: Safe alignment with overflow check
size_t safe_align(size_t size, size_t alignment) {
    if (alignment == 0) return size;

    // Check if size + (alignment - 1) would overflow
    if (size > SIZE_MAX - (alignment - 1)) {
        return 0;  // Indicate error
    }

    return (size + alignment - 1) & ~(alignment - 1);
}

// =============================================================================
// SECTION 10: Compound Expression Safety
// =============================================================================

// Variant 10.1: Check each step
void* safe_compound(uint32_t a, uint32_t b, uint32_t c) {
    // Check (a + b)
    if (a > UINT32_MAX - b) return nullptr;
    uint32_t sum = a + b;

    // Check sum * c
    if (c > 0 && sum > UINT32_MAX / c) return nullptr;
    size_t size = static_cast<size_t>(sum) * c;

    void* p = malloc(size);
    if (p) memset(p, 0, size);  // OK
    return p;
}

// Variant 10.2: Use saturating arithmetic
size_t saturating_mul(size_t a, size_t b) {
    if (a > 0 && b > SIZE_MAX / a) return SIZE_MAX;  // Saturate
    return a * b;
}

size_t saturating_add(size_t a, size_t b) {
    if (a > SIZE_MAX - b) return SIZE_MAX;  // Saturate
    return a + b;
}

int main() {
    return 0;
}
