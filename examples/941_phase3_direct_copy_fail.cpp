// 941_phase3_direct_copy_fail.cpp
// Tests: Phase 3 gap - missing base origin for direct parameter copy
//
// When p = base (no arithmetic), then p += off, the base origin
// should track base with accumulated offset.

#include <cstddef>
#include <cstring>

// Inner function - infers precondition on first param
void inner(char* buf, size_t n) {
    memcpy(buf, "data", n);
}

// Variant 1: Direct copy then compound assign
void variant1_copy_then_compound(char* base, size_t off, size_t n) {
    char* p = base;   // Direct copy - should establish base origin with offset 0
    p += off;         // Compound assign - should accumulate offset
    // Expected: base._size_bytes >= n+(off)
    inner(p, n);
}

// Variant 2: Direct copy then increment
void variant2_copy_then_increment(char* base, size_t n) {
    char* p = base;   // Direct copy - should establish base origin with offset 0
    ++p;              // Increment - should accumulate offset 1
    // Expected: base._size_bytes >= n+1
    inner(p, n);
}

// Variant 3: Direct copy then p = p + off
void variant3_copy_then_reassign_add(char* base, size_t off, size_t n) {
    char* p = base;   // Direct copy - should establish base origin with offset 0
    p = p + off;      // Reassign with add - should update offset
    // Expected: base._size_bytes >= n+(off)
    inner(p, n);
}

// Variant 4: Chain of direct copies
void variant4_chain_copies(char* base, size_t off, size_t n) {
    char* p = base;   // p has origin: base, offset 0
    char* q = p;      // q has origin: base, offset 0 (propagate)
    q += off;         // q has origin: base, offset off
    // Expected: base._size_bytes >= n+(off)
    inner(q, n);
}

// Variant 5: Direct copy, multiple increments
void variant5_multiple_increments(char* base, size_t n) {
    char* p = base;
    p++;              // offset 1
    p++;              // offset 2
    p++;              // offset 3
    // Expected: base._size_bytes >= n+3
    inner(p, n);
}

// Variant 6: Direct copy with immediate use (no offset)
void variant6_direct_use(char* base, size_t n) {
    char* p = base;   // Direct copy
    // Expected: base._size_bytes >= n (offset 0)
    inner(p, n);
}

int main() { return 0; }
