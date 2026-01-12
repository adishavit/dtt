// Example 615: Malloc provenance tracking
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a null dereference error is reported for a pointer assigned from
// malloc/calloc/realloc, DTT now emits an additional note showing the
// allocation call that could return NULL.
//
// Expected: FAIL (errors with provenance notes pointing to allocation calls)

#include <cstdlib>

void test_malloc_init() {
    // Variable initialization from malloc
    // ERROR: malloc may return NULL
    // NOTE: points to malloc call
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
}

void test_malloc_assign() {
    int* p;
    // Assignment from malloc
    // ERROR: malloc may return NULL
    // NOTE: points to malloc call
    p = (int*)malloc(sizeof(int));
    *p = 42;
}

void test_calloc() {
    // ERROR: calloc may return NULL
    // NOTE: points to calloc call
    int* p = (int*)calloc(10, sizeof(int));
    *p = 42;
}

void test_realloc() {
    int* p = (int*)malloc(sizeof(int));
    // ERROR: realloc may return NULL
    // NOTE: points to realloc call
    p = (int*)realloc(p, sizeof(int) * 2);
    *p = 42;
}

int main() {
    return 0;
}
