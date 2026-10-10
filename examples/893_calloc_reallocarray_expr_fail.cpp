// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 893_calloc_reallocarray_expr_fail.cpp
// Tests: Expression-tracked calloc/reallocarray sizes should be enforced in mem*/index bounds

#include <cstddef>
#include <cstring>
#include <cstdlib>

extern "C" void* reallocarray(void* ptr, size_t nmemb, size_t size);

// Variant 1: calloc with compound count, obvious overflow
void test_calloc_expr_overflow(size_t n) {
    unsigned char* buf = static_cast<unsigned char*>(calloc(n + 4, 1));
    if (!buf) return;
    memset(buf, 0, n + 10);  // ERROR: n + 10 > n + 4  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

// Variant 2: calloc with compound count, index one-past-end
void test_calloc_expr_index_oob(size_t n) {
    int* buf = static_cast<int*>(calloc(n + 1, sizeof(int)));
    if (!buf) return;
    buf[n + 1] = 0;  // ERROR: index == count  // dtt-error {{buffer index.*out of bounds}}
    free(buf);
}

// Variant 3: reallocarray with compound count, overflow
void test_reallocarray_expr_overflow(size_t n) {
    char* buf = static_cast<char*>(malloc(1));
    if (!buf) return;
    buf = static_cast<char*>(reallocarray(buf, n + 2, 1));
    if (!buf) return;
    memset(buf, 0, n + 5);  // ERROR: n + 5 > n + 2  // dtt-error {{(?:memcpy|memset|memcmp|memchr).*(?:buffer|bounds)}}
    free(buf);
}

// Variant 4: reallocarray with compound count, index one-past-end
void test_reallocarray_expr_index_oob(size_t n) {
    int* buf = static_cast<int*>(malloc(sizeof(int)));
    if (!buf) return;
    buf = static_cast<int*>(reallocarray(buf, n + 2, sizeof(int)));
    if (!buf) return;
    buf[n + 2] = 1;  // ERROR: index == count  // dtt-error {{buffer index.*out of bounds}}
    free(buf);
}

int main() { return 0; }
