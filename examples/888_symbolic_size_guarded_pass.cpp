// dtt-run: enforce
// 888_symbolic_size_guarded_pass.cpp
// Tests: Symbolic allocation sizes WITH proper guards should pass
// Change 2 + 3: Z3 should prove bounds when guards are present

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Variant 1: Guard ensures copy fits in symbolic buffer
void test_symbolic_guarded(size_t len) {
    char* buf = static_cast<char*>(malloc(len));
    if (!buf) return;
    if (len >= 13) {
        memcpy(buf, "hello world!", 13);  // OK: len >= 13 proven
    }
    free(buf);
}

// Variant 2: Guard on copy size
void test_copy_size_guarded(size_t buf_len, size_t copy_len) {
    char* buf = static_cast<char*>(malloc(buf_len));
    if (!buf) return;
    if (copy_len <= buf_len) {
        memcpy(buf, "source data here", copy_len);  // OK: copy_len <= buf_len
    }
    free(buf);
}

// Variant 3: Symbolic source with guard
void test_symbolic_src_guarded(size_t src_len, size_t copy_len) {
    char dst[64];
    char* src = static_cast<char*>(malloc(src_len));
    if (!src) return;
    if (copy_len <= src_len && copy_len <= sizeof(dst)) {
        memcpy(dst, src, copy_len);  // OK: both bounds proven
    }
    free(src);
}

// Variant 4: Allocation with expression
void test_alloc_expression_guarded(size_t n) {
    char* buf = static_cast<char*>(malloc(n + 10));
    if (!buf) return;
    if (n <= 10) {
        memcpy(buf, "0123456789", n);  // OK: n <= 10 <= n+10
    }
    free(buf);
}

// Variant 5: memset with guard
void test_memset_guarded(size_t len) {
    char* buf = static_cast<char*>(malloc(len));
    if (!buf) return;
    if (len >= 64) {
        memset(buf, 0, 64);  // OK: len >= 64 proven
    }
    free(buf);
}

int main() { return 0; }
