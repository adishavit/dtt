// 726_memory_functions_pass.cpp
// Memory functions (memset, memcpy, memmove) initialize their targets
// All patterns below are safe - memory is initialized before reading

#include <cstring>

// =============================================================================
// memset patterns (initializes memory)
// =============================================================================

// Pattern 1: memset scalar
int pattern1_memset_scalar() {
    int x;
    memset(&x, 0, sizeof(x));
    return x;  // OK: memset initialized x
}

// Pattern 2: memset array
int pattern2_memset_array() {
    int arr[10];
    memset(arr, 0, sizeof(arr));
    return arr[5];  // OK: memset initialized entire array
}

// Pattern 3: memset struct
struct Point { int x, y; };

int pattern3_memset_struct() {
    Point p;
    memset(&p, 0, sizeof(p));
    return p.x + p.y;  // OK: memset initialized all members
}

// Pattern 4: memset partial then use initialized part
int pattern4_memset_partial() {
    int arr[10];
    memset(arr, 0, 5 * sizeof(int));  // Only first 5 elements
    return arr[0];  // OK: arr[0] is in the initialized range
}

// =============================================================================
// memcpy patterns (copies initialized data)
// =============================================================================

// Pattern 5: memcpy from initialized source
int pattern5_memcpy_from_init() {
    int src = 42;
    int dst;
    memcpy(&dst, &src, sizeof(int));
    return dst;  // OK: memcpy copied initialized value
}

// Pattern 6: memcpy array
int pattern6_memcpy_array() {
    int src[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int dst[10];
    memcpy(dst, src, sizeof(dst));
    return dst[5];  // OK: memcpy initialized dst
}

// Pattern 7: memcpy struct
int pattern7_memcpy_struct() {
    Point src = {10, 20};
    Point dst;
    memcpy(&dst, &src, sizeof(Point));
    return dst.x + dst.y;  // OK: memcpy initialized dst
}

// =============================================================================
// memmove patterns (like memcpy but handles overlap)
// =============================================================================

// Pattern 8: memmove scalar
int pattern8_memmove_scalar() {
    int x;
    int src = 100;
    memmove(&x, &src, sizeof(int));
    return x;  // OK: memmove initialized x
}

// Pattern 9: memmove overlapping
int pattern9_memmove_overlap() {
    int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    memmove(&arr[2], &arr[0], 5 * sizeof(int));
    return arr[4];  // OK: memmove handles overlap correctly
}

// =============================================================================
// Combined patterns
// =============================================================================

// Pattern 10: memset then partial memcpy
int pattern10_memset_then_memcpy() {
    int arr[10];
    memset(arr, 0, sizeof(arr));  // Zero all
    int src[3] = {1, 2, 3};
    memcpy(arr, src, sizeof(src));  // Overwrite first 3
    return arr[0] + arr[5];  // OK: both initialized
}

// Pattern 11: Heap memset
int pattern11_heap_memset() {
    int* p = new int;
    memset(p, 0, sizeof(int));
    int x = *p;  // OK: memset initialized the heap object
    delete p;
    return x;
}

// Pattern 12: Heap array memset
int pattern12_heap_array_memset() {
    int* arr = new int[10];
    memset(arr, 0, 10 * sizeof(int));
    int x = arr[5];  // OK: memset initialized
    delete[] arr;
    return x;
}

int main() {
    pattern1_memset_scalar();
    pattern5_memcpy_from_init();
    pattern11_heap_memset();
    return 0;
}
