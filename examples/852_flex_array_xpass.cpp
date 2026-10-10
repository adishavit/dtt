// 852_flex_array_xpass.cpp
// GAP: DTT doesn't understand flexible array member pattern (data[1] at end of struct)
// The actual size is determined at allocation time, not from the [1] declaration

#include <cstdint>
#include <cstddef>

struct FlexArray {
    uint32_t count;
    uint8_t data[1];  // Flexible array member - size 1 is placeholder
};

// Variant 1: Safe access with total_size calculation
// DTT reports: buffer index 'i' may be out of bounds (buffer size is 1)
// FALSE POSITIVE: The actual size depends on how FlexArray was allocated
void safe_flex_array(const FlexArray* fa, size_t total_size) {
    if (!fa) return;
    if (total_size < sizeof(uint32_t)) return;

    size_t data_size = total_size - sizeof(uint32_t);
    size_t max_count = data_size;  // Each element is 1 byte

    uint32_t safe_count = (fa->count < max_count) ? fa->count : max_count;

    for (uint32_t i = 0; i < safe_count; i++) {
        uint8_t b = fa->data[i];  // GAP: DTT thinks data has size 1
        (void)b;
    }
}

int main() { return 0; }
