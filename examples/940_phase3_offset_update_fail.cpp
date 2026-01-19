// 940_phase3_offset_update_missing_xfail.cpp
// Gap: offset updates on compound/unary pointer arithmetic are gated on
// _base_length, so offsets from parameter-derived pointers don't accumulate.

#include <cstddef>
#include <cstring>

void inner_compound(char* base, size_t n, size_t extra) {
    char* p = base + 4;
    p += extra;  // Extra should be added to the base offset.
    memset(p, 0, n);
}

void inner_preinc(char* base, size_t n) {
    char* p = base + 4;
    ++p;  // Should add 1 to the base offset.
    memset(p, 0, n);
}

// Variant 1: missing extra in bound.
void outer_compound_missing_extra() {
    char buf[16];
    inner_compound(buf, 12, 2);  // Should warn: 16 < 12 + 4 + 2
}

// Variant 2: missing pre-increment.
void outer_preinc_missing() {
    char buf[16];
    inner_preinc(buf, 12);  // Should warn: 16 < 12 + 5
}

// Variant 3: second compound add should accumulate.
void inner_compound_twice(char* base, size_t n, size_t a, size_t b) {
    char* p = base + 2;
    p += a;
    p += b;
    memset(p, 0, n);
}

void outer_compound_twice_missing() {
    char buf[16];
    inner_compound_twice(buf, 12, 2, 4);  // Should warn: 16 < 12 + 2 + 2 + 4
}

int main() { return 0; }
