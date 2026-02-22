// 703_integer_overflow_size_fail.cpp
// Gap: DTT doesn't detect integer overflow in size calculations
// These overflows lead to undersized allocations and subsequent buffer overflows

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <climits>

// Variant 1: Simple multiplication overflow
void variant1_multiply_overflow(size_t count) {
    size_t element_size = sizeof(int);
    size_t total = count * element_size;  // BUG: can overflow if count is huge

    int* arr = (int*)malloc(total);
    if (!arr) return;

    // If overflow occurred, total is small, but we think we have 'count' elements
    for (size_t i = 0; i < count; i++) {
        arr[i] = (int)i;  // BUG: buffer overflow due to undersized allocation
    }
    free(arr);
}

// Variant 2: Addition overflow in size calculation
void variant2_addition_overflow(size_t len) {
    size_t total = len + 1;  // BUG: overflows if len == SIZE_MAX

    char* buf = (char*)malloc(total);
    if (!buf) return;

    buf[len] = '\0';  // BUG: if overflow, total is 0 or small
    free(buf);
}

// Variant 3: Header + data size overflow
struct Packet {
    uint32_t header;
    // data follows
};

void variant3_header_data_overflow(size_t data_len) {
    size_t total = sizeof(Packet) + data_len;  // BUG: can overflow

    Packet* pkt = (Packet*)malloc(total);
    if (!pkt) return;

    char* data = (char*)(pkt + 1);
    memset(data, 0, data_len);  // BUG: if overflow, buffer is too small
    free(pkt);
}

// Variant 4: Multiplication then addition overflow
void variant4_compound_overflow(size_t rows, size_t cols) {
    size_t row_size = cols * sizeof(int);  // First overflow point
    size_t total = rows * row_size;        // Second overflow point

    int* matrix = (int*)malloc(total);
    if (!matrix) return;

    for (size_t r = 0; r < rows; r++) {
        for (size_t c = 0; c < cols; c++) {
            matrix[r * cols + c] = 0;  // BUG: OOB if overflow occurred
        }
    }
    free(matrix);
}

// Variant 5: Signed integer overflow becoming negative
void variant5_signed_overflow(int count) {
    int total = count * (int)sizeof(int);  // BUG: signed overflow is UB
                                            // Result could be negative

    if (total <= 0) return;  // Doesn't catch all overflow cases

    int* arr = (int*)malloc((size_t)total);
    if (!arr) return;

    for (int i = 0; i < count; i++) {
        arr[i] = i;  // BUG: if count is huge but total overflowed to small positive
    }
    free(arr);
}

// Variant 6: Overflow in calloc-style calculation
void variant6_calloc_style_overflow(size_t nmemb, size_t size) {
    // Manual calloc implementation without overflow check
    size_t total = nmemb * size;  // BUG: can overflow

    void* ptr = malloc(total);
    if (!ptr) return;

    memset(ptr, 0, total);  // Uses the (possibly overflowed) total
    // But if we then access nmemb elements of size bytes, we overflow
    free(ptr);
}

// Variant 7: Overflow in realloc growth
void variant7_realloc_growth_overflow(char* buf, size_t current_size) {
    size_t new_size = current_size * 2;  // BUG: overflow if current_size > SIZE_MAX/2

    char* new_buf = (char*)realloc(buf, new_size);
    if (!new_buf) {
        free(buf);
        return;
    }
    // If overflow, new_size is small, but code assumes it's 2x bigger
    new_buf[current_size] = 'x';  // BUG: may be OOB
    free(new_buf);
}

// Variant 8: Overflow via shift
void variant8_shift_overflow(size_t count) {
    size_t size = count << 4;  // Multiply by 16 via shift - can overflow

    char* buf = (char*)malloc(size);
    if (!buf) return;

    for (size_t i = 0; i < count * 16; i++) {
        buf[i] = 0;  // BUG: if shift overflowed
    }
    free(buf);
}

// Variant 9: Overflow in alignment calculation
void variant9_alignment_overflow(size_t size) {
    // Round up to 16-byte alignment
    size_t aligned = (size + 15) & ~(size_t)15;  // BUG: size+15 can overflow

    char* buf = (char*)malloc(aligned);
    if (!buf) return;

    memset(buf, 0, size);  // Original size, not aligned - could be larger
    free(buf);
}

// Variant 10: Overflow in array-of-structs
struct Element {
    int data[100];
};

void variant10_struct_array_overflow(size_t count) {
    size_t total = count * sizeof(Element);  // BUG: overflow with large count

    Element* arr = (Element*)malloc(total);
    if (!arr) return;

    for (size_t i = 0; i < count; i++) {
        arr[i].data[0] = (int)i;  // BUG: OOB if overflow
    }
    free(arr);
}

// Variant 11: Overflow in string concatenation size
void variant11_string_concat_overflow(const char* s1, const char* s2) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    size_t total = len1 + len2 + 1;  // BUG: can overflow

    char* result = (char*)malloc(total);
    if (!result) return;

    strcpy(result, s1);
    strcat(result, s2);  // BUG: if overflow, buffer too small
    free(result);
}

// Variant 12: Overflow with 32-bit intermediate
void variant12_32bit_intermediate(uint32_t width, uint32_t height) {
    uint32_t pixels = width * height;  // 32-bit overflow
    size_t size = pixels * 4;          // Even if this doesn't overflow...

    char* buf = (char*)malloc(size);
    if (!buf) return;

    // The 32-bit pixels value may have overflowed
    for (uint32_t i = 0; i < pixels; i++) {
        buf[i * 4] = 0;  // BUG: if width*height overflowed
    }
    free(buf);
}

// Variant 13: Safe case - proper overflow check
void variant13_safe_with_check(size_t count, size_t elem_size) {
    // Check for overflow before multiplication
    if (count > 0 && elem_size > SIZE_MAX / count) {
        return;  // Would overflow
    }

    size_t total = count * elem_size;
    char* buf = (char*)malloc(total);
    if (!buf) return;

    memset(buf, 0, total);  // OK: no overflow
    free(buf);
}

// Variant 14: Overflow in power-of-2 rounding
void variant14_power_of_2_overflow(size_t size) {
    // Round up to next power of 2
    size_t rounded = 1;
    while (rounded < size) {
        rounded *= 2;  // BUG: can overflow to 0 if size is very large
    }

    char* buf = (char*)malloc(rounded);
    if (!buf) return;  // If rounded==0, malloc(0) may return non-null!

    memset(buf, 0, size);  // Original size - if rounded overflowed, this crashes
    free(buf);
}

// Variant 15: Overflow in image processing
void variant15_image_size_overflow(uint32_t width, uint32_t height, uint32_t channels) {
    // Common pattern in image processing
    size_t row_stride = (size_t)width * channels;
    size_t total = row_stride * height;  // Two potential overflow points

    uint8_t* pixels = (uint8_t*)malloc(total);
    if (!pixels) return;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            for (uint32_t c = 0; c < channels; c++) {
                pixels[y * row_stride + x * channels + c] = 0;  // BUG: OOB if overflow
            }
        }
    }
    free(pixels);
}

int main() {
    // These demonstrate integer overflow vulnerabilities
    // that lead to undersized allocations and buffer overflows
    // DTT should catch these by tracking size expressions
    return 0;
}
