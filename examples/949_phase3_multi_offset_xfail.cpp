// 949_phase3_multi_offset_xfail.cpp
// Known limitation: Only first offset param recorded.
// Multiple offset parameters (p = buf + a + b) only track first param.

#include <cstddef>
#include <cstring>

// Pattern: cumulative offsets from multiple parameters
void inner_two_offsets(char* buf, size_t a, size_t b) {
    char* p = buf + a;
    p = p + b;  // Second offset contribution
    memset(p, 0, 1);
    // Should infer: buf._size_bytes >= 1 + a + b
    // Currently infers: buf._size_bytes >= 1 + a (loses b)
}

// Call site should warn when total offset exceeds buffer
void outer_two_offsets_bad() {
    char arr[10];
    inner_two_offsets(arr, 4, 8);  // Should warn: 10 < 1 + 4 + 8 = 13
    // But with only 'a' tracked: 10 >= 1 + 4 = 5 (no warning - gap!)
}

int main() { return 0; }
