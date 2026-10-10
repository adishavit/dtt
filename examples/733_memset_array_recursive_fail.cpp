// 733_memset_array_recursive_fail.cpp
// GAP: markMemoryFunctionDstInitialized doesn't recursively mark array elements
// memset on an array should mark all elements as initialized

void* memset(void*, int, unsigned long);
void* memcpy(void*, const void*, unsigned long);

// =============================================================================
// memset on arrays
// =============================================================================

// Variant 1: memset on int array, read element
int variant1_memset_array() {
    int arr[10];
    memset(arr, 0, sizeof(arr));
    return arr[5];  // SAFE: arr[5] should be initialized by memset
}

// Variant 2: memset on struct array
struct Point { int x, y; };

int variant2_memset_struct_array() {
    Point pts[5];
    memset(pts, 0, sizeof(pts));
    return pts[2].x;  // SAFE: pts[2].x should be initialized
}

// Variant 3: Partial memset
int variant3_partial_memset() {
    int arr[10];
    memset(arr, 0, 5 * sizeof(int));  // Only first 5 elements
    return arr[3];  // Should be OK: within memset range
    // arr[7] would still be uninitialized
}

// Variant 4: memset with variable size
int variant4_variable_size(int n) {
    int arr[100];
    memset(arr, 0, n * sizeof(int)); // dtt-error {{cannot verify memset size}}
    return arr[0];  // KNOWN GAP (not asserted): arr[0] should be initialized if n > 0
}

// =============================================================================
// memcpy on arrays
// =============================================================================

// Variant 5: memcpy to array
int variant5_memcpy_array() {
    int src[5] = {1, 2, 3, 4, 5};
    int dst[5];
    memcpy(dst, src, sizeof(dst));
    return dst[2];  // SAFE: dst[2] should be initialized by memcpy
}

// Variant 6: memcpy partial array
int variant6_memcpy_partial() {
    int src[10] = {0};
    int dst[10];
    memcpy(dst, src, 5 * sizeof(int));  // Only first 5 elements
    return dst[2];  // Should be OK: within memcpy range
    // dst[7] would still be uninitialized
}

// =============================================================================
// Nested structures
// =============================================================================

struct Nested {
    int arr[5];
    Point p;
};

// Variant 7: memset nested struct with array
int variant7_memset_nested() {
    Nested n;
    memset(&n, 0, sizeof(n));
    return n.arr[2];  // SAFE: n.arr[2] should be initialized
}

// Variant 8: memset nested, read all parts
int variant8_memset_nested_all() {
    Nested n;
    memset(&n, 0, sizeof(n));
    return n.arr[0] + n.arr[4] + n.p.x + n.p.y;  // SAFE: all should be initialized
}

// =============================================================================
// Multi-dimensional arrays
// =============================================================================

// Variant 9: memset 2D array
int variant9_memset_2d() {
    int arr[3][3];
    memset(arr, 0, sizeof(arr));
    return arr[1][1];  // SAFE: arr[1][1] should be initialized
}

// Variant 10: memset 3D array
int variant10_memset_3d() {
    int arr[2][2][2];
    memset(arr, 0, sizeof(arr));
    return arr[1][1][1];  // SAFE: should be initialized
}

// =============================================================================
// Safe patterns (should work when gap is fixed)
// =============================================================================

// Safe 1: Explicit element init after memset (redundant but safe)
int safe1_explicit_after_memset() {
    int arr[10];
    memset(arr, 0, sizeof(arr));
    arr[5] = 42;  // Explicit init (redundant)
    return arr[5];  // OK
}

// Safe 2: Value-initialized array (no memset needed)
int safe2_value_init() {
    int arr[10] = {};
    return arr[5];  // OK: value-initialized
}

int main() {
    return 0;
}
