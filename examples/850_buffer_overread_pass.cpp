// 850_buffer_overread_pass.cpp
// Tests: Safe patterns that PREVENT buffer over-read vulnerabilities

#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

// =============================================================================
// SECTION 1: memcpy with Source Size Validation
// =============================================================================

// Variant 1.1: Check source size before copy
void safe_memcpy_checked(const char* src, size_t src_size,
                         char* dst, size_t dst_size,
                         size_t copy_len) {
    if (copy_len > src_size) return;  // Prevent over-read
    if (copy_len > dst_size) return;  // Prevent over-write

    memcpy(dst, src, copy_len);  // OK: both validated
}

// Variant 1.2: Use min of all sizes
void safe_memcpy_min(const void* src, size_t src_size,
                     void* dst, size_t dst_size) {
    size_t copy_size = std::min(src_size, dst_size);
    memcpy(dst, src, copy_size);  // OK: clamped to minimum
}

// Variant 1.3: Validate struct member access
void safe_struct_member() {
    struct Data {
        char name[16];
        int value;
    } d = {"test", 42};

    char buf[100];
    memcpy(buf, d.name, sizeof(d.name));  // OK: uses actual size
}

// =============================================================================
// SECTION 2: memcmp with Length Validation
// =============================================================================

// Variant 2.1: Validate both buffer sizes
int safe_memcmp(const void* a, size_t a_len,
                const void* b, size_t b_len,
                size_t cmp_len) {
    if (cmp_len > a_len) return -2;
    if (cmp_len > b_len) return -2;

    return memcmp(a, b, cmp_len);  // OK: validated
}

// Variant 2.2: Compare minimum of sizes
int safe_memcmp_min(const void* a, size_t a_len,
                    const void* b, size_t b_len) {
    size_t cmp_len = std::min(a_len, b_len);
    return memcmp(a, b, cmp_len);  // OK: uses minimum
}

// =============================================================================
// SECTION 3: memchr with Bounds
// =============================================================================

// Variant 3.1: Search within actual buffer size
const void* safe_memchr(const void* buf, size_t buf_size,
                        int c, size_t search_len) {
    if (search_len > buf_size) search_len = buf_size;
    return memchr(buf, c, search_len);  // OK: clamped
}

// =============================================================================
// SECTION 4: Array Access with Bounds Check
// =============================================================================

// Variant 4.1: Check index before access
char safe_index(const char* arr, size_t arr_size, size_t index) {
    if (index >= arr_size) return '\0';  // Bounds check
    return arr[index];  // OK: validated
}

// Variant 4.2: Early return on bad index
bool safe_index_early(const char* arr, size_t size, size_t index, char* out) {
    if (index >= size) return false;
    *out = arr[index];  // OK: validated
    return true;
}

// =============================================================================
// SECTION 5: Loop with Validated Bounds
// =============================================================================

// Variant 5.1: Use actual size as bound
void safe_loop_bound(const uint8_t* data, size_t actual_len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < actual_len; i++) {
        sum += data[i];  // OK: i < actual_len always
    }
}

// Variant 5.2: Min of claimed and actual
void safe_loop_min(const uint8_t* data, size_t actual_len, uint32_t claimed_len) {
    size_t safe_len = std::min(static_cast<size_t>(claimed_len), actual_len);
    for (size_t i = 0; i < safe_len; i++) {
        uint8_t b = data[i];  // OK: bounded by actual
        (void)b;
    }
}

// =============================================================================
// SECTION 6: Protocol Parsing with Validation
// =============================================================================

// Variant 6.1: Validate length field
void safe_protocol_parse(const uint8_t* data, size_t data_len) {
    if (data_len < 3) return;

    uint16_t length = (data[1] << 8) | data[2];
    size_t available = data_len - 3;

    if (length > available) return;  // Reject if too large

    uint8_t local[256];
    if (length <= sizeof(local)) {
        memcpy(local, data + 3, length);  // OK: validated
    }
}

// =============================================================================
// SECTION 7: Flexible Array with Total Size
// =============================================================================

struct FlexArray {
    uint32_t count;
    uint8_t data[1];
};

// Variant 7.1: Calculate max elements from total size
void safe_flex_array(const FlexArray* fa, size_t total_size) {
    if (total_size < sizeof(uint32_t)) return;

    size_t data_size = total_size - sizeof(uint32_t);
    size_t max_count = data_size;  // Each element is 1 byte

    uint32_t safe_count = (fa->count < max_count) ? fa->count : max_count;

    for (uint32_t i = 0; i < safe_count; i++) {
        uint8_t b = fa->data[i];  // OK: validated
        (void)b;
    }
}

// =============================================================================
// SECTION 8: Image Processing with Validated Dimensions
// =============================================================================

struct ImageHeader {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_offset;
    uint32_t pixel_size;
};

// Variant 8.1: Validate all offsets and sizes
void safe_image_parse(const uint8_t* file_data, size_t file_size) {
    if (file_size < sizeof(ImageHeader)) return;

    const ImageHeader* hdr = reinterpret_cast<const ImageHeader*>(file_data);

    // Validate offset is within file
    if (hdr->pixel_offset > file_size) return;

    // Validate size doesn't exceed remaining data
    size_t remaining = file_size - hdr->pixel_offset;
    if (hdr->pixel_size > remaining) return;

    uint8_t pixels[65536];
    if (hdr->pixel_size <= sizeof(pixels)) {
        memcpy(pixels, file_data + hdr->pixel_offset, hdr->pixel_size);  // OK
    }
}

// =============================================================================
// SECTION 9: Crypto Operations with Size Validation
// =============================================================================

// Variant 9.1: Validate key length before copy
void safe_key_extract(const uint8_t* blob, size_t blob_size,
                      uint8_t key_len, uint8_t* key_out, size_t key_out_size) {
    if (4 + key_len > blob_size) return;  // Validate source
    if (key_len > key_out_size) return;   // Validate dest

    memcpy(key_out, blob + 4, key_len);  // OK: double validated
}

// =============================================================================
// SECTION 10: Struct Padding Safety
// =============================================================================

// Variant 10.1: Zero struct before use
struct PaddedStruct {
    uint8_t a;
    uint32_t b;
    uint16_t c;
};

void safe_padding() {
    PaddedStruct s = {};  // Zero-initialize including padding
    s.a = 1;
    s.b = 2;
    s.c = 3;
    // Padding is zeroed, no leak if serialized
}

// Variant 10.2: Use memset for safety
void safe_padding_memset() {
    PaddedStruct s;
    memset(&s, 0, sizeof(s));  // Clear all including padding
    s.a = 1;
    s.b = 2;
    s.c = 3;
}

int main() {
    return 0;
}
