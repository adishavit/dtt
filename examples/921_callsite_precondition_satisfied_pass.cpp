// 921_callsite_precondition_satisfied_pass.cpp
// Tests: Call-site checking where preconditions ARE provable (Phase 2)
//
// These tests have inner functions with inferred preconditions, and outer
// functions that SATISFY those preconditions. When Phase 2 is implemented,
// these should NOT produce warnings at the call site.

#include <cstddef>
#include <cstdlib>
#include <cstring>

// ============================================================================
// BASIC SATISFIED PRECONDITIONS
// ============================================================================

// Inner function infers: buf._size_bytes >= n
void inner_memset(char* buf, size_t n) {
    memset(buf, 0, n);
}

// Variant 1: Same symbolic size - trivially satisfied
void outer_same_size(size_t n) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    // OK: buf._size_bytes = n, need n >= n (trivially true)
    inner_memset(buf, n);
    free(buf);
}

// Variant 2: Larger buffer - satisfied
void outer_larger_buffer(size_t n) {
    char* buf = (char*)malloc(n + 100);
    if (!buf) return;
    // OK: buf._size_bytes = n+100, need n+100 >= n (true)
    inner_memset(buf, n);
    free(buf);
}

// Variant 3: Constant size that fits
void outer_const_fits() {
    char buf[100];
    // OK: buf._size_bytes = 100, need 100 >= 50 (true)
    inner_memset(buf, 50);
}

// Variant 4: Guarded size
void outer_guarded(size_t buf_size, size_t use_size) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    if (use_size <= buf_size) {
        // OK: Guarded - proved use_size <= buf_size
        inner_memset(buf, use_size);
    }
    free(buf);
}

// ============================================================================
// MEMCPY WITH BOTH BUFFERS SIZED
// ============================================================================

void inner_memcpy(char* dst, const char* src, size_t n) {
    memcpy(dst, src, n);
}

// Variant 5: Both buffers same size
void outer_both_same_size(size_t n) {
    char* dst = (char*)malloc(n);
    char* src = (char*)malloc(n);
    if (!dst || !src) { free(dst); free(src); return; }
    // OK: Both buffers are size n, copying n bytes
    inner_memcpy(dst, src, n);
    free(dst);
    free(src);
}

// Variant 6: Both buffers larger than copy size
void outer_both_larger(size_t buf_size, size_t copy_size) {
    if (copy_size > buf_size) return;  // Guard
    char* dst = (char*)malloc(buf_size);
    char* src = (char*)malloc(buf_size);
    if (!dst || !src) { free(dst); free(src); return; }
    // OK: Guarded - copy_size <= buf_size
    inner_memcpy(dst, src, copy_size);
    free(dst);
    free(src);
}

// ============================================================================
// EXPRESSION SIZES THAT MATCH
// ============================================================================

void inner_expr(char* buf, size_t n, size_t extra) {
    memset(buf, 0, n + extra);
}

// Variant 7: Buffer sized to match expression
void outer_expr_match(size_t n, size_t extra) {
    char* buf = (char*)malloc(n + extra);
    if (!buf) return;
    // OK: buf._size_bytes = n+extra, need n+extra >= n+extra
    inner_expr(buf, n, extra);
    free(buf);
}

// Variant 8: Buffer larger than expression
void outer_expr_larger(size_t n, size_t extra) {
    char* buf = (char*)malloc(n + extra + 50);
    if (!buf) return;
    // OK: buf._size_bytes = n+extra+50 >= n+extra
    inner_expr(buf, n, extra);
    free(buf);
}

// ============================================================================
// CONSTANT PROPAGATION
// ============================================================================

// Variant 9: Both constant
void outer_const_const() {
    char buf[200];
    // OK: 200 >= 100
    inner_memset(buf, 100);
}

// Variant 10: Constant buffer, symbolic but bounded size
void outer_const_bounded(size_t n) {
    char buf[100];
    if (n <= 100) {
        // OK: Guarded
        inner_memset(buf, n);
    }
}

// ============================================================================
// CALLOC AND REALLOC
// ============================================================================

// Variant 11: Calloc with matching size
void outer_calloc_match(size_t count) {
    int* buf = (int*)calloc(count, sizeof(int));
    if (!buf) return;
    // OK: buf._size_bytes = count*sizeof(int), use same
    inner_memset((char*)buf, count * sizeof(int));
    free(buf);
}

// Variant 12: Realloc to exact size
void outer_realloc_exact(size_t n) {
    char* buf = (char*)malloc(10);
    if (!buf) return;
    buf = (char*)realloc(buf, n);
    if (!buf) return;
    // OK: After realloc, buf._size_bytes = n
    inner_memset(buf, n);
    free(buf);
}

// ============================================================================
// NESTED CALLS WITH SIZE PRESERVATION
// ============================================================================

void level2_inner(char* buf, size_t n) {
    memset(buf, 0, n);
}

void level1_pass(char* buf, size_t n) {
    // Just passes through - same size
    level2_inner(buf, n);
}

// Variant 13: Nested with same size
void outer_nested_same(size_t n) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    // OK: Size preserved through calls
    level1_pass(buf, n);
    free(buf);
}

// ============================================================================
// STRUCT MEMBERS WITH KNOWN SIZE
// ============================================================================

struct FixedBuffer {
    char data[256];
};

// Variant 14: Struct member constant size
void outer_struct_const() {
    FixedBuffer fb;
    // OK: fb.data._size_bytes = 256 >= 100
    inner_memset(fb.data, 100);
}

// Variant 15: Struct member with guard
void outer_struct_guarded(size_t n) {
    FixedBuffer fb;
    if (n <= 256) {
        // OK: Guarded
        inner_memset(fb.data, n);
    }
}

// ============================================================================
// VECTOR WITH SIZE TRACKING
// ============================================================================

#include <vector>

// Variant 16: Vector resized to match
void outer_vector_match(size_t n) {
    std::vector<char> v(n);  // v._size = n
    // OK: v._size = n, copying n bytes
    inner_memset(v.data(), n);
}

// Variant 17: Vector larger than needed
void outer_vector_larger(size_t n) {
    std::vector<char> v(n + 100);
    // OK: v._size = n+100 >= n
    inner_memset(v.data(), n);
}

// ============================================================================
// MULTIPLICATION MATCHES
// ============================================================================

// Variant 18: Element count * size matches
void outer_mul_match(size_t count) {
    int* buf = (int*)malloc(count * sizeof(int));
    if (!buf) return;
    // OK: buf._size_bytes = count*sizeof(int)
    inner_memset((char*)buf, count * sizeof(int));
    free(buf);
}

// ============================================================================
// TEMPLATE WITH MATCHING SIZE
// ============================================================================

template<size_t N>
void inner_template_fixed(char (&buf)[N], size_t n) {
    if (n <= N) memset(buf, 0, n);  // Guarded in template
}

// Variant 19: Template with known size
void outer_template() {
    char buf[100];
    // OK: Template knows size
    inner_template_fixed(buf, 50);
}

// ============================================================================
// DIVISION THAT FITS
// ============================================================================

// Variant 20: Size divided from buffer size
void outer_div_fits(size_t n) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    // OK: n/2 <= n (for n > 0)
    inner_memset(buf, n / 2);
    free(buf);
}

// ============================================================================
// MIN/MAX PATTERNS
// ============================================================================

inline size_t my_min(size_t a, size_t b) { return a < b ? a : b; }

// Variant 21: Using min to ensure fit
void outer_min_pattern(size_t buf_size, size_t requested) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    size_t actual = my_min(requested, buf_size);
    // OK: actual <= buf_size by construction
    inner_memset(buf, actual);
    free(buf);
}

// ============================================================================
// ASSERT/ASSUME PATTERNS
// ============================================================================

// Variant 22: Assert before call (may be tracked as proof)
void outer_assert(char* buf, size_t buf_size, size_t use_size) {
    // Assuming buf was allocated with buf_size
    if (use_size > buf_size) __builtin_unreachable();
    // OK: Asserted use_size <= buf_size
    inner_memset(buf, use_size);
}

// ============================================================================
// LOOP WITH CONSTANT BOUND
// ============================================================================

// Variant 23: Loop with constant iterations
void outer_loop_const() {
    char buf[100];
    for (int i = 0; i < 5; i++) {
        // OK: 20 < 100
        inner_memset(buf, 20);
    }
}

// ============================================================================
// RECURSIVE WITH SIZE SHRINKING
// ============================================================================

void recursive_shrink(char* buf, size_t n, int depth) {
    if (depth <= 0 || n == 0) return;
    memset(buf, 0, n);
    // Each recursive call uses smaller size
    recursive_shrink(buf, n / 2, depth - 1);
}

// Variant 24: Recursive with shrinking size
void outer_recursive_shrink(size_t n) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    // OK: First call uses n, subsequent use n/2, n/4, etc.
    recursive_shrink(buf, n, 10);
    free(buf);
}

// ============================================================================
// MEMBER FUNCTION WITH MATCHING SIZE
// ============================================================================

class SafeBuffer {
    char* data_;
    size_t size_;
public:
    SafeBuffer(size_t n) : data_((char*)malloc(n)), size_(n) {}
    ~SafeBuffer() { free(data_); }

    void fill(size_t n) {
        if (n <= size_ && data_) {
            inner_memset(data_, n);  // OK: Guarded
        }
    }
};

// Variant 25: Class method with internal guard
void outer_class_method(size_t n) {
    SafeBuffer sb(100);
    // OK: fill() internally guards
    sb.fill(n);
}

int main() { return 0; }
