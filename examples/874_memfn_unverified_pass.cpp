// 874_memfn_unverified_pass.cpp
// Tests: Guarded runtime sizes should NOT produce warnings
// When bounds are checked before the call, DTT should accept the code

#include <cstring>
#include <cstddef>

// Variant 1: Guarded memcpy with if check
void test_guarded_memcpy(size_t n) {
    char buf[256];
    if (n <= sizeof(buf)) {
        memcpy(buf, "data", n);  // OK: guarded by n <= 256
    }
}

// Variant 2: Guarded memset
void test_guarded_memset(size_t n) {
    char buf[64];
    if (n <= 64) {
        memset(buf, 0, n);  // OK: guarded
    }
}

// Variant 3: Constant size is always safe when within bounds
void test_constant_safe() {
    char buf[100];
    memcpy(buf, "hello", 5);  // OK: constant 5 <= 100
    memset(buf, 0, 50);       // OK: constant 50 <= 100
}

// Variant 4: sizeof ensures exact fit
void test_sizeof_safe() {
    char src[100], dst[100];
    memcpy(dst, src, sizeof(src));  // OK: sizeof matches
}

int main() { return 0; }
