// 726_memory_functions_xfail.cpp
// Gap: DTT doesn't yet recognize memset/memcpy as initializers
// These patterns should be safe but DTT will report false positives
// until memory function tracking is implemented

#include <cstring>

// =============================================================================
// memset should initialize but DTT doesn't track it yet
// =============================================================================

// Variant 1: memset scalar - should be OK but DTT warns
int variant1_memset_scalar() {
    int x;
    memset(&x, 0, sizeof(x));
    return x;  // FALSE POSITIVE: DTT thinks x is uninitialized
}

// Variant 2: memset array
int variant2_memset_array() {
    int arr[10];
    memset(arr, 0, sizeof(arr));
    return arr[5];  // FALSE POSITIVE: DTT thinks arr[5] is uninitialized
}

// Variant 3: memset struct
struct Point { int x, y; };

int variant3_memset_struct() {
    Point p;
    memset(&p, 0, sizeof(p));
    return p.x;  // FALSE POSITIVE
}

// =============================================================================
// memcpy should initialize destination but DTT doesn't track it yet
// =============================================================================

// Variant 4: memcpy initializes dst
int variant4_memcpy() {
    int src = 42;
    int dst;
    memcpy(&dst, &src, sizeof(int));
    return dst;  // FALSE POSITIVE
}

// Variant 5: memcpy array
int variant5_memcpy_array() {
    int src[5] = {1, 2, 3, 4, 5};
    int dst[5];
    memcpy(dst, src, sizeof(dst));
    return dst[2];  // FALSE POSITIVE
}

// =============================================================================
// memmove should initialize but DTT doesn't track it yet
// =============================================================================

// Variant 6: memmove initializes dst
int variant6_memmove() {
    int src = 100;
    int x;
    memmove(&x, &src, sizeof(int));
    return x;  // FALSE POSITIVE
}

// =============================================================================
// Heap patterns with memory functions
// =============================================================================

// Variant 7: heap memset
int variant7_heap_memset() {
    int* p = new int;
    memset(p, 0, sizeof(int));
    int x = *p;  // FALSE POSITIVE: heap + memset not tracked
    delete p;
    return x;
}

// Variant 8: heap memcpy
int variant8_heap_memcpy() {
    int src = 42;
    int* p = new int;
    memcpy(p, &src, sizeof(int));
    int x = *p;  // FALSE POSITIVE
    delete p;
    return x;
}

int main() {
    return 0;
}
