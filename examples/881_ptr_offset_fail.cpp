// 881_ptr_offset_fail.cpp
// Tests: Pointer arithmetic overflow - DTT should catch these
// Phase 1d: CVE-2023-38545 (curl SOCKS5) style patterns

#include <cstring>
#include <cstddef>

// =============================================================================
// SECTION 1: Offset reduces capacity below copy size
// =============================================================================

// Variant 1.1: CVE-2023-38545 pattern - offset leaves insufficient room
void test_ptr_overflow() {
    char buf[100];
    char* p = buf + 90;  // p has capacity 10
    memcpy(p, "hello world", 12);  // ERROR: 12 > 10
}

// Variant 1.2: Large offset, small copy still overflows
void test_ptr_small_overflow() {
    char buf[100];
    char* p = buf + 99;  // p has capacity 1
    memcpy(p, "ab", 2);  // ERROR: 2 > 1
}

// Variant 1.3: Double offset overflow
void test_ptr_double_offset_overflow() {
    char buf[100];
    char* p = buf + 50;   // p has capacity 50
    char* q = p + 40;     // q has capacity 10
    memcpy(q, "0123456789X", 11);  // ERROR: 11 > 10
}

// Variant 1.4: memset overflow with offset
void test_ptr_memset_overflow() {
    char buf[64];
    char* p = buf + 60;  // p has capacity 4
    memset(p, 'x', 10);  // ERROR: 10 > 4
}

// Variant 1.5: memmove overflow with offset
void test_ptr_memmove_overflow() {
    char buf[50];
    char* p = buf + 45;  // p has capacity 5
    memmove(p, "testing", 8);  // ERROR: 8 > 5
}

// Variant 1.6: strcpy overflow with offset
void test_ptr_strcpy_overflow() {
    char buf[20];
    char* p = buf + 15;  // p has capacity 5
    strcpy(p, "hello");  // ERROR: 6 > 5
}

int main() { return 0; }
