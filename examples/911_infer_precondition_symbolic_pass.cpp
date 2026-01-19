// 911_infer_precondition_symbolic_pass.cpp
// Tests: Inferred precondition notes with symbolic-tracked buffers
//
// When DTT cannot verify bounds for mem* functions with symbolic sizes,
// it emits a note showing the inferred precondition. This test verifies
// the feature works across many scenarios.
//
// For untracked plain pointers, DTT skips bounds checking (no information).
// For tracked buffers (malloc, calloc, etc.), DTT can emit symbolic
// unverified warnings when it has size info but can't prove bounds.

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

// ============================================================================
// MEMSET VARIANTS (destination buffer)
// ============================================================================

// Variant 1: malloc'd buffer, symbolic copy size
void memset_malloc_symbolic_size(size_t buf_size, size_t fill_size) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= fill_size
    memset(buf, 0, fill_size);
    free(buf);
}

// Variant 2: malloc'd buffer, expression copy size
void memset_malloc_expr_size(size_t buf_size, size_t n) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= (n + 10)
    memset(buf, 0xFF, n + 10);
    free(buf);
}

// Variant 3: calloc'd buffer, symbolic size
void memset_calloc_symbolic(size_t count, size_t elem_size, size_t fill_size) {
    int* buf = (int*)calloc(count, elem_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= fill_size
    memset(buf, 0, fill_size);
    free(buf);
}

// Variant 4: realloc'd buffer
void memset_realloc_symbolic(size_t old_size, size_t new_size, size_t fill_size) {
    char* buf = (char*)malloc(old_size);
    if (!buf) return;
    buf = (char*)realloc(buf, new_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= fill_size
    memset(buf, 'A', fill_size);
    free(buf);
}

// Variant 5: __builtin_memset
void memset_builtin_symbolic(size_t buf_size, size_t fill_size) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= fill_size
    __builtin_memset(buf, 0, fill_size);
    free(buf);
}

// ============================================================================
// MEMCPY VARIANTS (destination and source)
// ============================================================================

// Variant 6: memcpy destination symbolic
void memcpy_dst_malloc_symbolic(size_t dst_size, size_t copy_size) {
    char* dst = (char*)malloc(dst_size);
    char src[100] = {0};
    if (!dst) return;
    // Inferred: dst._size_bytes >= copy_size
    memcpy(dst, src, copy_size);
    free(dst);
}

// Variant 7: memcpy source symbolic (Heartbleed-style)
void memcpy_src_malloc_symbolic(size_t src_size, size_t copy_size) {
    char dst[100];
    char* src = (char*)malloc(src_size);
    if (!src) return;
    // Inferred: src._size_bytes >= copy_size
    memcpy(dst, src, copy_size);
    free(src);
}

// Variant 8: memcpy both symbolic
void memcpy_both_symbolic(size_t dst_size, size_t src_size, size_t copy_size) {
    char* dst = (char*)malloc(dst_size);
    char* src = (char*)malloc(src_size);
    if (!dst || !src) { free(dst); free(src); return; }
    // Inferred: dst._size_bytes >= copy_size
    // Inferred: src._size_bytes >= copy_size
    memcpy(dst, src, copy_size);
    free(dst);
    free(src);
}

// Variant 9: memcpy with expression size
void memcpy_expr_size(size_t dst_size, size_t n, size_t offset) {
    char* dst = (char*)malloc(dst_size);
    char src[200] = {0};
    if (!dst) return;
    // Inferred: dst._size_bytes >= (n + offset)
    memcpy(dst, src, n + offset);
    free(dst);
}

// Variant 10: __builtin_memcpy
void memcpy_builtin_symbolic(size_t dst_size, size_t copy_size) {
    char* dst = (char*)malloc(dst_size);
    char src[100] = {0};
    if (!dst) return;
    // Inferred: dst._size_bytes >= copy_size
    __builtin_memcpy(dst, src, copy_size);
    free(dst);
}

// ============================================================================
// MEMMOVE VARIANTS
// ============================================================================

// Variant 11: memmove destination symbolic
void memmove_dst_symbolic(size_t dst_size, size_t move_size) {
    char* dst = (char*)malloc(dst_size);
    char src[100] = {0};
    if (!dst) return;
    // Inferred: dst._size_bytes >= move_size
    memmove(dst, src, move_size);
    free(dst);
}

// Variant 12: memmove source symbolic
void memmove_src_symbolic(size_t src_size, size_t move_size) {
    char dst[100];
    char* src = (char*)malloc(src_size);
    if (!src) return;
    // Inferred: src._size_bytes >= move_size
    memmove(dst, src, move_size);
    free(src);
}

// Variant 13: memmove overlapping (same buffer)
void memmove_overlap_symbolic(size_t buf_size, size_t move_size, size_t offset) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= move_size (for both dst and src)
    memmove(buf + offset, buf, move_size);
    free(buf);
}

// Variant 14: __builtin_memmove
void memmove_builtin_symbolic(size_t dst_size, size_t move_size) {
    char* dst = (char*)malloc(dst_size);
    char src[100] = {0};
    if (!dst) return;
    // Inferred: dst._size_bytes >= move_size
    __builtin_memmove(dst, src, move_size);
    free(dst);
}

// ============================================================================
// MEMCMP VARIANTS (both buffers)
// ============================================================================

// Variant 15: memcmp first buffer symbolic
void memcmp_s1_symbolic(size_t s1_size, size_t cmp_size) {
    char* s1 = (char*)malloc(s1_size);
    char s2[100] = {0};
    if (!s1) return;
    // Inferred: s1._size_bytes >= cmp_size
    volatile int r = memcmp(s1, s2, cmp_size);
    (void)r;
    free(s1);
}

// Variant 16: memcmp second buffer symbolic
void memcmp_s2_symbolic(size_t s2_size, size_t cmp_size) {
    char s1[100] = {0};
    char* s2 = (char*)malloc(s2_size);
    if (!s2) return;
    // Inferred: s2._size_bytes >= cmp_size
    volatile int r = memcmp(s1, s2, cmp_size);
    (void)r;
    free(s2);
}

// Variant 17: memcmp both symbolic
void memcmp_both_symbolic(size_t s1_size, size_t s2_size, size_t cmp_size) {
    char* s1 = (char*)malloc(s1_size);
    char* s2 = (char*)malloc(s2_size);
    if (!s1 || !s2) { free(s1); free(s2); return; }
    // Inferred: s1._size_bytes >= cmp_size
    // Inferred: s2._size_bytes >= cmp_size
    volatile int r = memcmp(s1, s2, cmp_size);
    (void)r;
    free(s1);
    free(s2);
}

// Variant 18: __builtin_memcmp
void memcmp_builtin_symbolic(size_t s1_size, size_t cmp_size) {
    char* s1 = (char*)malloc(s1_size);
    char s2[100] = {0};
    if (!s1) return;
    // Inferred: s1._size_bytes >= cmp_size
    volatile int r = __builtin_memcmp(s1, s2, cmp_size);
    (void)r;
    free(s1);
}

// ============================================================================
// MEMCHR VARIANTS
// ============================================================================

// Variant 19: memchr symbolic buffer
void memchr_symbolic_buffer(size_t buf_size, size_t search_size) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= search_size
    volatile void* r = memchr(buf, 'X', search_size);
    (void)r;
    free(buf);
}

// Variant 20: memchr expression size
void memchr_expr_size(size_t buf_size, size_t n) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= (n * 2)
    volatile void* r = memchr(buf, 'Y', n * 2);
    (void)r;
    free(buf);
}

// Variant 21: __builtin_memchr
void memchr_builtin_symbolic(size_t buf_size, size_t search_size) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= search_size
    volatile void* r = __builtin_memchr(buf, 'Z', search_size);
    (void)r;
    free(buf);
}

// ============================================================================
// CALLOC VARIANTS (element count * size)
// ============================================================================

// Variant 22: calloc with symbolic element count
void calloc_count_symbolic(size_t count, size_t copy_size) {
    int* buf = (int*)calloc(count, sizeof(int));
    if (!buf) return;
    // Inferred: buf._size_bytes >= copy_size
    memset(buf, 0, copy_size);
    free(buf);
}

// Variant 23: calloc with expression count
void calloc_expr_count(size_t n, size_t copy_size) {
    char* buf = (char*)calloc(n + 5, 1);
    if (!buf) return;
    // Inferred: buf._size_bytes >= copy_size
    memset(buf, 'A', copy_size);
    free(buf);
}

// ============================================================================
// REALLOCARRAY VARIANTS (if available)
// ============================================================================

extern "C" void* reallocarray(void* ptr, size_t nmemb, size_t size);

// Variant 24: reallocarray symbolic
void reallocarray_symbolic(size_t count, size_t copy_size) {
    char* buf = (char*)malloc(1);
    if (!buf) return;
    buf = (char*)reallocarray(buf, count, 1);
    if (!buf) return;
    // Inferred: buf._size_bytes >= copy_size
    memset(buf, 0, copy_size);
    free(buf);
}

// Variant 25: reallocarray with expression
void reallocarray_expr(size_t n, size_t copy_size) {
    int* buf = (int*)malloc(sizeof(int));
    if (!buf) return;
    buf = (int*)reallocarray(buf, n + 2, sizeof(int));
    if (!buf) return;
    // Inferred: buf._size_bytes >= copy_size
    memset(buf, 0xFF, copy_size);
    free(buf);
}

// ============================================================================
// STL CONTAINER VARIANTS (vector data())
// ============================================================================

// Variant 26: vector data() as source
void vector_data_as_source(std::vector<char>& v, size_t copy_size) {
    char dst[100];
    // v's size is tracked, copy_size is symbolic
    // Heartbleed-style: reading from container with symbolic size
    memcpy(dst, v.data(), copy_size);
}

// Variant 27: vector data() with expression size
void vector_data_expr_size(std::vector<int>& v, size_t n) {
    char dst[400];
    // Inferred: v.data()._size_bytes >= (n * sizeof(int))
    memcpy(dst, v.data(), n * sizeof(int));
}

// ============================================================================
// COMPLEX EXPRESSION VARIANTS
// ============================================================================

// Variant 28: multiplication in size
void complex_mul_size(size_t buf_size, size_t count, size_t elem_size) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= (count * elem_size)
    memset(buf, 0, count * elem_size);
    free(buf);
}

// Variant 29: addition in size
void complex_add_size(size_t buf_size, size_t base, size_t extra) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= (base + extra)
    memcpy(buf, "test", base + extra);
    free(buf);
}

// Variant 30: subtraction in size
void complex_sub_size(size_t buf_size, size_t total, size_t header) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= (total - header)
    memset(buf, 0, total - header);
    free(buf);
}

// Variant 31: division in size
void complex_div_size(size_t buf_size, size_t total_bits) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= (total_bits / 8)
    memset(buf, 0, total_bits / 8);
    free(buf);
}

// Variant 32: bitwise AND in size (alignment)
void complex_bitand_size(size_t buf_size, size_t size, size_t mask) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Inferred: buf._size_bytes >= (size & mask)
    memset(buf, 0, size & mask);
    free(buf);
}

// ============================================================================
// NESTED FUNCTION CALL SIZE
// ============================================================================

inline size_t get_size(size_t n) { return n; }

// Variant 33: size from function call
void size_from_function(size_t buf_size, size_t n) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // Size comes from function call - may be tracked as unknown
    memset(buf, 0, get_size(n));
    free(buf);
}

// ============================================================================
// TYPED BUFFERS (different element types)
// ============================================================================

// Variant 34: int buffer with byte operations
void int_buffer_byte_ops(size_t count, size_t byte_count) {
    int* buf = (int*)malloc(count * sizeof(int));
    if (!buf) return;
    // Inferred: buf._size_bytes >= byte_count
    memset(buf, 0, byte_count);
    free(buf);
}

// Variant 35: double buffer
void double_buffer_ops(size_t count, size_t byte_count) {
    double* buf = (double*)malloc(count * sizeof(double));
    if (!buf) return;
    // Inferred: buf._size_bytes >= byte_count
    memset(buf, 0, byte_count);
    free(buf);
}

// Variant 36: struct buffer
struct Data { int x; char y[20]; };

void struct_buffer_ops(size_t count, size_t byte_count) {
    Data* buf = (Data*)malloc(count * sizeof(Data));
    if (!buf) return;
    // Inferred: buf._size_bytes >= byte_count
    memset(buf, 0, byte_count);
    free(buf);
}

int main() { return 0; }
