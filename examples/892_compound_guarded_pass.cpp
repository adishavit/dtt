// 892_compound_guarded_pass.cpp
// Tests: Compound expressions with proper guards should be provably safe
// After fix: Z3 should prove these bounds

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Variant 1: malloc(n + 10) with guard n <= 90, copy 100 bytes
// Buffer is n + 10 >= 100 when n >= 90, but we guard n <= 90
// So buffer is at least 100 bytes, copy of 100 is safe
void test_compound_alloc_guarded(size_t n) {
    char* buf = static_cast<char*>(malloc(n + 10));
    if (!buf) return;
    if (n >= 90) {  // n >= 90 means buf >= 100
        memcpy(buf, "x", 100);  // OK: (n + 10) >= 100 when n >= 90
    }
    free(buf);
}

// Variant 2: Compound allocation with constant copy
// malloc(n + 100) guarantees at least 100 bytes
void test_compound_min_guaranteed(size_t n) {
    char* buf = static_cast<char*>(malloc(n + 100));
    if (!buf) return;
    // Copy 50 bytes - always safe since buffer is at least 100 bytes
    memcpy(buf, "hello world! this is a test string!", 50);  // OK: 50 <= n + 100
    free(buf);
}

// Variant 3: Compound allocation, simple copy with sufficient guard
void test_simple_copy_compound_alloc(size_t n) {
    char* buf = static_cast<char*>(malloc(n + 100));
    if (!buf) return;
    if (n >= 0) {  // Always true, but buf is at least 100 bytes
        memcpy(buf, "hello world!", 13);  // OK: 13 <= 100 <= n + 100
    }
    free(buf);
}

// Variant 4: Both compound, guard makes it safe
void test_both_compound_guarded(size_t len) {
    char* buf = static_cast<char*>(malloc(len + 20));
    if (!buf) return;
    if (len <= 10) {
        // Copy len + 10 into buffer of len + 20
        // When len <= 10: copy <= 20, buffer >= 20
        memcpy(buf, "x", len + 10);  // OK: (len + 10) <= (len + 20)
    }
    free(buf);
}

int main() { return 0; }
