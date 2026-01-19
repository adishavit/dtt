// 920_callsite_precondition_fail.cpp
// Tests: Call-site checking of inferred preconditions (Phase 2)
//
// When a function has inferred preconditions (from Phase 1.5), DTT should
// check those preconditions at each call site within the same TU.
// Phase 2 is now implemented - these tests verify DTT catches call-site violations.
//
// The pattern:
// 1. inner() has unverified mem* call → infers precondition
// 2. outer() calls inner() → should check if precondition is provable
// 3. If not provable → emit warning at call site

#include <cstddef>
#include <cstdlib>
#include <cstring>

// ============================================================================
// BASIC CALL-SITE CHECKING - SHOULD WARN (precondition not provable)
// ============================================================================

// Inner function infers: buf._size_bytes >= n
void inner_memset(char* buf, size_t n) {
    memset(buf, 0, n);  // Phase 1.5: infers buf._size_bytes >= n
}

// Variant 1: Call with untracked pointer - cannot prove precondition
void outer_untracked(char* p, size_t sz) {
    // GAP: Should warn - cannot prove p._size_bytes >= sz
    inner_memset(p, sz);
}

// Variant 2: Call with smaller buffer - cannot prove precondition
void outer_smaller_buffer(size_t sz) {
    char buf[10];
    // GAP: Should warn - cannot prove buf._size_bytes (10) >= sz
    inner_memset(buf, sz);
}

// Variant 3: Call with symbolic size mismatch
void outer_size_mismatch(size_t buf_size, size_t call_size) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // GAP: Should warn - cannot prove buf._size_bytes (buf_size) >= call_size
    inner_memset(buf, call_size);
    free(buf);
}

// ============================================================================
// MEMCPY VARIANTS - BOTH DST AND SRC PRECONDITIONS
// ============================================================================

// Inner function infers: dst._size_bytes >= n AND src._size_bytes >= n
void inner_memcpy(char* dst, const char* src, size_t n) {
    memcpy(dst, src, n);  // Infers preconditions on both buffers
}

// Variant 4: Neither buffer can be proven
void outer_memcpy_both_unknown(char* d, const char* s, size_t sz) {
    // GAP: Should warn - cannot prove d._size_bytes >= sz
    // GAP: Should warn - cannot prove s._size_bytes >= sz
    inner_memcpy(d, s, sz);
}

// Variant 5: Destination tracked but source unknown
void outer_memcpy_dst_only(size_t dst_size, const char* src, size_t copy_size) {
    char* dst = (char*)malloc(dst_size);
    if (!dst) return;
    // GAP: Should warn - cannot prove src._size_bytes >= copy_size
    inner_memcpy(dst, src, copy_size);
    free(dst);
}

// Variant 6: Source tracked but destination unknown
void outer_memcpy_src_only(char* dst, size_t src_size, size_t copy_size) {
    char* src = (char*)malloc(src_size);
    if (!src) return;
    // GAP: Should warn - cannot prove dst._size_bytes >= copy_size
    inner_memcpy(dst, src, copy_size);
    free(src);
}

// ============================================================================
// EXPRESSION SIZE VARIANTS
// ============================================================================

// Inner with expression in size
void inner_expr_size(char* buf, size_t n, size_t extra) {
    memset(buf, 0, n + extra);  // Infers: buf._size_bytes >= (n + extra)
}

// Variant 7: Call with different expression
void outer_expr_mismatch(size_t buf_size, size_t n, size_t m) {
    char* buf = (char*)malloc(buf_size);
    if (!buf) return;
    // GAP: Should warn - cannot prove buf_size >= (n + m)
    inner_expr_size(buf, n, m);
    free(buf);
}

// ============================================================================
// NESTED CALLS
// ============================================================================

void level2(char* buf, size_t n) {
    memset(buf, 0, n);  // Infers: buf._size_bytes >= n
}

void level1(char* buf, size_t n) {
    level2(buf, n);  // GAP: Should propagate/check precondition
}

// Variant 8: Two levels of calls
void outer_nested(char* p, size_t sz) {
    // GAP: Should warn - cannot prove p._size_bytes >= sz
    level1(p, sz);
}

// ============================================================================
// CONDITIONAL CALLS
// ============================================================================

// Variant 9: Call in one branch
void outer_conditional(char* buf, size_t n, bool flag) {
    if (flag) {
        // GAP: Should warn in this branch
        inner_memset(buf, n);
    }
}

// Variant 10: Call in loop
void outer_loop(char* buf, size_t n, int count) {
    for (int i = 0; i < count; i++) {
        // GAP: Should warn
        inner_memset(buf, n);
    }
}

// ============================================================================
// DIFFERENT BUFFER SOURCES
// ============================================================================

// Variant 11: Stack array to function expecting tracked buffer
void outer_stack_array(size_t n) {
    char local[50];
    // GAP: Should warn - cannot prove 50 >= n
    inner_memset(local, n);
}

// Variant 12: Global buffer
char global_buf[100];

void outer_global(size_t n) {
    // GAP: Should warn - cannot prove 100 >= n
    inner_memset(global_buf, n);
}

// Variant 13: Struct member
struct Container { char data[64]; };

void outer_struct_member(Container& c, size_t n) {
    // GAP: Should warn - cannot prove 64 >= n
    inner_memset(c.data, n);
}

// ============================================================================
// FUNCTION POINTER / INDIRECT CALLS (should not check - can't know callee)
// ============================================================================

typedef void (*MemsetFn)(char*, size_t);

// Variant 14: Function pointer - should NOT warn (can't know preconditions)
void outer_fnptr(MemsetFn fn, char* buf, size_t n) {
    // No warning expected - can't infer callee's preconditions
    fn(buf, n);
}

// ============================================================================
// TEMPLATE INSTANTIATION
// ============================================================================

template<typename T>
void inner_template(T* buf, size_t n) {
    memset(buf, 0, n);  // Infers: buf._size_bytes >= n
}

// Variant 15: Template call
void outer_template(size_t n) {
    int arr[10];
    // GAP: Should warn - cannot prove sizeof(int)*10 >= n
    inner_template(arr, n);
}

// ============================================================================
// MULTIPLE PARAMETERS WITH PRECONDITIONS
// ============================================================================

void inner_multi(char* buf1, char* buf2, size_t n) {
    memcpy(buf1, buf2, n);  // Infers preconditions on both
}

// Variant 16: Same buffer passed twice
void outer_same_buffer(char* buf, size_t n) {
    // GAP: Should warn - cannot prove buf._size_bytes >= n (for both)
    inner_multi(buf, buf, n);
}

// Variant 17: Overlapping with offset
void outer_overlapping(char* buf, size_t n, size_t offset) {
    // GAP: Should warn
    inner_multi(buf + offset, buf, n);
}

// ============================================================================
// SIZE EXPRESSIONS AT CALL SITE
// ============================================================================

// Variant 18: Constant size at call site
void outer_const_size(char* buf) {
    // GAP: Should warn - buf is untracked, size is 100
    inner_memset(buf, 100);
}

// Variant 19: Expression size at call site
void outer_expr_call_size(char* buf, size_t a, size_t b) {
    // GAP: Should warn - cannot prove buf._size_bytes >= (a * b)
    inner_memset(buf, a * b);
}

// Variant 20: Size from sizeof
void outer_sizeof_call(char* buf) {
    struct Big { char data[1000]; };
    // GAP: Should warn - cannot prove buf._size_bytes >= sizeof(Big)
    inner_memset(buf, sizeof(Big));
}

// ============================================================================
// CROSS-FUNCTION BUFFER CREATION
// ============================================================================

char* create_buffer(size_t size) {
    return (char*)malloc(size);
}

// Variant 21: Buffer from another function
void outer_buffer_from_fn(size_t create_size, size_t use_size) {
    char* buf = create_buffer(create_size);
    if (!buf) return;
    // GAP: Should warn - cannot prove create_size >= use_size
    // (Tracking may be lost across function call)
    inner_memset(buf, use_size);
    free(buf);
}

// ============================================================================
// VOID POINTER CASTS
// ============================================================================

void inner_void(void* buf, size_t n) {
    memset(buf, 0, n);
}

// Variant 22: Cast to void*
void outer_void_cast(size_t n) {
    int arr[10];
    // GAP: Should warn
    inner_void(arr, n);
}

// ============================================================================
// RETURN VALUE USED AS BUFFER
// ============================================================================

// Variant 23: Return value from malloc chain
void outer_malloc_chain(size_t alloc_size, size_t use_size) {
    void* p = malloc(alloc_size);
    char* buf = (char*)p;
    if (!buf) return;
    // GAP: Should warn - cannot prove alloc_size >= use_size
    inner_memset(buf, use_size);
    free(buf);
}

// ============================================================================
// ARITHMETIC ON SIZE PARAMETER
// ============================================================================

// Variant 24: Caller divides size
void outer_divide_size(char* buf, size_t total) {
    // GAP: Should warn - buf untracked
    inner_memset(buf, total / 2);
}

// Variant 25: Caller subtracts from size
void outer_subtract_size(char* buf, size_t total, size_t header) {
    // GAP: Should warn
    inner_memset(buf, total - header);
}

// ============================================================================
// STANDARD LIBRARY INTERACTIONS
// ============================================================================

#include <vector>
#include <string>

// Variant 26: Vector resize then call
void outer_vector_resize(size_t n) {
    std::vector<char> v;
    v.resize(50);  // v._size = 50
    // GAP: Should warn - cannot prove 50 >= n
    inner_memset(v.data(), n);
}

// Variant 27: String data
void outer_string_data(std::string& s, size_t n) {
    // GAP: Should warn - s._size is tracked but may not be >= n
    inner_memset(const_cast<char*>(s.data()), n);
}

// ============================================================================
// RECURSIVE CALLS
// ============================================================================

// Variant 28: Recursive function with precondition
void recursive_inner(char* buf, size_t n, int depth) {
    if (depth <= 0) return;
    memset(buf, 0, n);  // Infers precondition
    recursive_inner(buf, n, depth - 1);  // GAP: Should check here too
}

void outer_recursive(char* buf, size_t n) {
    // GAP: Should warn
    recursive_inner(buf, n, 5);
}

// ============================================================================
// MEMBER FUNCTION CALLS
// ============================================================================

class Buffer {
public:
    void fill(char* buf, size_t n) {
        memset(buf, 0, n);  // Infers precondition
    }
};

// Variant 29: Method call
void outer_method(Buffer& b, char* buf, size_t n) {
    // GAP: Should warn
    b.fill(buf, n);
}

// ============================================================================
// LAMBDA CALLS
// ============================================================================

// Variant 30: Lambda with precondition
void outer_lambda(char* buf, size_t n) {
    auto fill = [](char* b, size_t sz) {
        memset(b, 0, sz);  // Infers precondition
    };
    // GAP: Should warn (but lambdas may not be tracked)
    fill(buf, n);
}

int main() { return 0; }
