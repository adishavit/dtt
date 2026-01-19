// 894_calloc_reallocarray_expr_pass.cpp
// Tests: Expression-tracked calloc/reallocarray sizes with provably safe uses

#include <cstddef>
#include <cstring>
#include <cstdlib>

extern "C" void* reallocarray(void* ptr, size_t nmemb, size_t size);

// Variant 1: calloc size matches write size
void test_calloc_expr_safe(size_t n) {
    unsigned char* buf = static_cast<unsigned char*>(calloc(n + 4, 1));
    if (!buf) return;
    memset(buf, 0, n + 4);  // OK: n + 4 <= n + 4
    free(buf);
}

// Variant 2: calloc with compound count, index within bounds
void test_calloc_expr_index_safe(size_t n) {
    int* buf = static_cast<int*>(calloc(n + 1, sizeof(int)));
    if (!buf) return;
    buf[n] = 0;  // OK: n < n + 1
    free(buf);
}

// Variant 3: reallocarray with compound count, safe write
void test_reallocarray_expr_safe(size_t n) {
    char* buf = static_cast<char*>(malloc(1));
    if (!buf) return;
    buf = static_cast<char*>(reallocarray(buf, n + 2, 1));
    if (!buf) return;
    memset(buf, 0, n + 1);  // OK: n + 1 < n + 2
    free(buf);
}

// Variant 4: reallocarray with compound count, index within bounds
void test_reallocarray_expr_index_safe(size_t n) {
    int* buf = static_cast<int*>(malloc(sizeof(int)));
    if (!buf) return;
    buf = static_cast<int*>(reallocarray(buf, n + 2, sizeof(int)));
    if (!buf) return;
    buf[n + 1] = 1;  // OK: n + 1 < n + 2
    free(buf);
}

int main() { return 0; }
