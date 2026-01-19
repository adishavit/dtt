// 948_phase3_offset_negated_xfail.cpp
// Gap: When tracking offset param for p = buf + off (addition), call site
// correctly computes bound + off*elemSize.
// But for p = end - off (subtraction), call site should compute bound - off*elemSize.
// Currently, we may always ADD the offset regardless of original operation.

#include <cstddef>
#include <cstring>

// Pattern: sliding window from end of buffer
// end points to end of valid region, p = end - off gives start of write region
void inner_from_end(char* end, size_t off) {
    char* p = end - off;  // p is 'off' bytes BEFORE end
    memset(p, 0, off);    // Writes exactly 'off' bytes starting at p
    // Inferred precondition should be: end._size_bytes >= off - off = 0
    // Actually, we need 'off' bytes available BEFORE 'end'
}

// Simple addition pattern for comparison
void inner_from_start(char* start, size_t off) {
    char* p = start + off;
    memset(p, 0, 1);
    // Inferred: start._size_bytes >= 1 + off
}

// Test: subtraction pattern with parameter offset
void outer_sub_pattern() {
    char arr[10];
    char* end = arr + 10;     // end points past the array
    inner_from_end(end, 8);   // p = end - 8 = arr + 2, writes 8 bytes
    // If DTT incorrectly ADDS offset: would check 10 >= 8 + 8 = 16 (fail - wrong)
    // Correct behavior: check available bytes before end >= off
}

// Test: addition pattern for comparison
void outer_add_pattern_bad() {
    char arr[4];
    inner_from_start(arr, 4); // Should warn: 4 < 1 + 4 = 5
}

int main() { return 0; }
