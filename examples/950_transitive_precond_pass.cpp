// 950_transitive_precond_pass.cpp
// Tests: Transitive precondition propagation through call chains
//
// When inner() calls memcpy and infers buf._size_bytes >= n,
// and outer() calls inner(buf, n), then outer() inherits that precondition.
// Callers passing undersized buffers to outer() get warnings.
//
// Expected: Warnings at lines 28, 47, 73, 87 about buffer sizes.

#include <cstring>
#include <cstdlib>

// Level 2: Direct memcpy - infers buf._size_bytes >= n
static void level2_memcpy(char* buf, size_t n) {
    memcpy(buf, "data", n);  // Infers: buf._size_bytes >= n
}

// Level 1: Calls level2 - should inherit precondition
static void level1_wrapper(char* buf, size_t n) {
    level2_memcpy(buf, n);  // Call site checked, but level1 should also get precond
}

// Level 0: Calls level1 with undersized buffer - SHOULD FAIL
// GAP: DTT should catch this but doesn't because level1 has no precondition
static void level0_violation(size_t n) {
    char small[4];
    level1_wrapper(small, n);  // BUG: small is only 4 bytes, n could be larger
}

// Variant 2: Three-level chain
static void deep_level3(char* dst, const char* src, size_t n) {
    memcpy(dst, src, n);
}

static void deep_level2(char* dst, const char* src, size_t n) {
    deep_level3(dst, src, n);
}

static void deep_level1(char* dst, const char* src, size_t n) {
    deep_level2(dst, src, n);
}

static void deep_violation(size_t n) {
    char buf[8];
    const char* src = "hello world this is a long string";
    deep_level1(buf, src, n);  // BUG: buf is only 8 bytes
}

// Variant 3: Diamond pattern (A calls B and C, both call D)
static void diamond_leaf(char* buf, size_t n) {
    memset(buf, 0, n);
}

static void diamond_left(char* buf, size_t n) {
    diamond_leaf(buf, n);
}

static void diamond_right(char* buf, size_t n) {
    diamond_leaf(buf, n);
}

static void diamond_top(char* buf, size_t n, bool flag) {
    if (flag) {
        diamond_left(buf, n);
    } else {
        diamond_right(buf, n);
    }
}

static void diamond_violation(size_t n) {
    char small[2];
    diamond_top(small, n, true);  // BUG: small is only 2 bytes
}

// Variant 4: Precondition with expression (n + offset)
static void expr_inner(char* buf, size_t n, size_t offset) {
    memset(buf + offset, 0, n);  // Infers: buf._size_bytes >= n + offset
}

static void expr_outer(char* buf, size_t n, size_t off) {
    expr_inner(buf, n, off);  // Should propagate: buf._size_bytes >= n + off
}

static void expr_violation(size_t n) {
    char buf[10];
    expr_outer(buf, n, 5);  // BUG: buf is 10, but we need n + 5
}

int main() {
    level0_violation(100);
    deep_violation(100);
    diamond_violation(100);
    expr_violation(100);
    return 0;
}
