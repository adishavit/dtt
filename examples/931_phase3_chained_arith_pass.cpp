// 931_phase3_chained_arith_pass.cpp
// Tests: Phase 3 - Chained pointer arithmetic
//
// When q = p + b and p = base + a, DTT should accumulate offsets:
// base._size_bytes >= n + (a + b)

#include <cstddef>
#include <cstring>

// Variant 1: Two-level chain
void inner_chain2(char* base, size_t a, size_t b, size_t n) {
    char* p = base + a;
    char* q = p + b;
    memcpy(q, "data", n);  // Should infer: base._size_bytes >= n + a + b
}

// Variant 2: Three-level chain
void inner_chain3(char* base, size_t a, size_t b, size_t c, size_t n) {
    char* p = base + a;
    char* q = p + b;
    char* r = q + c;
    memcpy(r, "data", n);  // Should infer: base._size_bytes >= n + a + b + c
}

// Variant 3: Chain with constants
void inner_chain_const(char* base, size_t n) {
    char* p = base + 5;
    char* q = p + 10;
    memcpy(q, "data", n);  // Should infer: base._size_bytes >= n + 15
}

int main() { return 0; }
