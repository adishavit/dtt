// 942_phase3_compound_untracked_fail.cpp
// Tests: Phase 3 gap - compound assign on untracked parameter pointer
//
// When base is a raw parameter (no _base_length), p += off should still
// update base origin offset for precondition rewriting.

#include <cstddef>
#include <cstring>

// Inner function - infers precondition on first param
void inner(char* buf, size_t n) {
    memcpy(buf, "data", n);
}

// Variant 1: Direct parameter with compound assign
void variant1_param_compound(char* base, size_t off, size_t n) {
    base += off;      // Compound assign directly on parameter
    // Expected: base._size_bytes >= n+(off) [original base]
    inner(base, n);
}

// Variant 2: Parameter increment
void variant2_param_increment(char* base, size_t n) {
    ++base;           // Increment directly on parameter
    // Expected: base._size_bytes >= n+1 [original base]
    inner(base, n);
}

// Variant 3: Parameter with multiple compound assigns
void variant3_param_multi_compound(char* base, size_t a, size_t b, size_t n) {
    base += a;
    base += b;
    // Expected: base._size_bytes >= n+(a)+(b) [original base]
    inner(base, n);
}

// Variant 4: Parameter subtract
void variant4_param_subtract(char* base, size_t off, size_t back, size_t n) {
    base += off;
    base -= back;
    // Expected: base._size_bytes >= n+(off-back) [original base]
    inner(base, n);
}

// Variant 5: Unary decrement
void variant5_param_decrement(char* base, size_t n) {
    --base;           // Decrement - negative offset
    // This is unusual but should track: base._size_bytes >= n+(-1)
    inner(base, n);
}

// Variant 6: Post-increment
void variant6_post_increment(char* base, size_t n) {
    char* old = base++;  // Post-increment - base moves, old stays
    // old should have origin: base, offset 0
    // base should have origin: base, offset 1 (but it's the same var...)
    // Actually this is tricky - base IS the parameter
    inner(old, n);       // Expected: base._size_bytes >= n
}

int main() { return 0; }
