// 880_ptr_offset_pass.cpp
// Tests: Pointer arithmetic with safe bounds - should NOT produce errors
// Phase 1d: Pointer offset tracking for remaining capacity

#include <cstring>
#include <cstddef>

// =============================================================================
// SECTION 1: Safe pointer offsets
// =============================================================================

// Variant 1.1: Small copy into offset pointer with room
void test_ptr_offset_safe() {
    char buf[100];
    char* p = buf + 90;  // p has capacity 10
    memcpy(p, "hello", 5);  // OK: 5 <= 10
}

// Variant 1.2: Exact fit
void test_ptr_offset_exact() {
    char buf[100];
    char* p = buf + 90;
    memcpy(p, "0123456789", 10);  // OK: 10 == 10
}

// Variant 1.3: Zero offset (full buffer)
void test_ptr_no_offset() {
    char buf[100];
    char* p = buf;  // p has capacity 100
    memcpy(p, "test", 50);  // OK: 50 <= 100
}

// Variant 1.4: Multiple offsets
void test_ptr_double_offset() {
    char buf[100];
    char* p = buf + 50;   // p has capacity 50
    char* q = p + 40;     // q has capacity 10
    memcpy(q, "hi", 2);   // OK: 2 <= 10
}

// Variant 1.5: memset with offset
void test_ptr_offset_memset() {
    char buf[64];
    char* p = buf + 60;
    memset(p, 0, 4);  // OK: 4 <= 4
}

int main() { return 0; }
