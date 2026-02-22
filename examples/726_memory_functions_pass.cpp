// 726: Memory functions as initializers — safe patterns
// DTT Tier 5.3 recognizes memset/memcpy/memmove as initializers via
// markMemoryFunctionDstInitialized(). No false positives expected.

#include <cstring>

// =============================================================================
// memset initializes destination
// =============================================================================

// Variant 1: memset scalar
int variant1_memset_scalar() {
    int x;
    memset(&x, 0, sizeof(x));
    return x;  // OK: memset initializes x
}

// Variant 2: memset array
int variant2_memset_array() {
    int arr[10];
    memset(arr, 0, sizeof(arr));
    return arr[5];  // OK: memset initializes arr
}

// Variant 3: memset struct
struct Point { int x, y; };

int variant3_memset_struct() {
    Point p;
    memset(&p, 0, sizeof(p));
    return p.x;  // OK: memset initializes p
}

// =============================================================================
// memcpy initializes destination
// =============================================================================

// Variant 4: memcpy initializes dst
int variant4_memcpy() {
    int src = 42;
    int dst;
    memcpy(&dst, &src, sizeof(int));
    return dst;  // OK: memcpy initializes dst
}

// Variant 5: memcpy array
int variant5_memcpy_array() {
    int src[5] = {1, 2, 3, 4, 5};
    int dst[5];
    memcpy(dst, src, sizeof(dst));
    return dst[2];  // OK: memcpy initializes dst
}

// =============================================================================
// memmove initializes destination
// =============================================================================

// Variant 6: memmove initializes dst
int variant6_memmove() {
    int src = 100;
    int x;
    memmove(&x, &src, sizeof(int));
    return x;  // OK: memmove initializes x
}

// =============================================================================
// Heap patterns with memory functions
// =============================================================================

// Variant 7: heap memset
int variant7_heap_memset() {
    int* p = new int;
    memset(p, 0, sizeof(int));
    int x = *p;  // OK: heap + memset tracked
    delete p;
    return x;
}

// Variant 8: heap memcpy
int variant8_heap_memcpy() {
    int src = 42;
    int* p = new int;
    memcpy(p, &src, sizeof(int));
    int x = *p;  // OK: heap + memcpy tracked
    delete p;
    return x;
}

int main() {
    return 0;
}
