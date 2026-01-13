// 711_loop_bound_edge_cases_pass.cpp
// Edge case tests for loop bound tracking
// These patterns should all compile without DTT warnings

#include <cstddef>
#include <cstdint>

// ============================================================================
// CONSTEXPR BOUNDS - DTT should recognize constexpr as compile-time constants
// ============================================================================

// Variant 1: Basic constexpr
constexpr int CONST_SIZE = 50;
void variant1_constexpr_basic() {
    int arr[CONST_SIZE] = {0};
    for (int i = 0; i < CONST_SIZE; i++) {
        arr[i] = i * 2;  // SAFE: i < CONST_SIZE == 50
    }
}

// Variant 2: constexpr with namespace
namespace config {
    constexpr size_t BUFFER_SIZE = 100;
}
void variant2_constexpr_namespace() {
    char buf[config::BUFFER_SIZE] = {0};
    for (size_t i = 0; i < config::BUFFER_SIZE; i++) {
        buf[i] = 'x';  // SAFE: i < 100
    }
}

// Variant 3: constexpr from expression
constexpr int ROWS = 10;
constexpr int COLS = 20;
constexpr int TOTAL = ROWS * COLS;
void variant3_constexpr_computed() {
    int data[TOTAL] = {0};
    for (int i = 0; i < TOTAL; i++) {
        data[i] = i;  // SAFE: i < 200
    }
}

// Variant 4: static const (treated like constexpr for integral types)
static const int STATIC_SIZE = 30;
void variant4_static_const() {
    int arr[STATIC_SIZE] = {0};
    for (int i = 0; i < STATIC_SIZE; i++) {
        arr[i] = i;  // SAFE: i < 30
    }
}

// Variant 5: enum constant as bound
enum { ENUM_SIZE = 25 };
void variant5_enum_constant() {
    int arr[ENUM_SIZE] = {0};
    for (int i = 0; i < ENUM_SIZE; i++) {
        arr[i] = i;  // SAFE: i < 25
    }
}

// Variant 6: Template non-type parameter
template<size_t N>
void variant6_template_param() {
    int arr[N] = {0};
    for (size_t i = 0; i < N; i++) {
        arr[i] = (int)i;  // SAFE: i < N
    }
}

// ============================================================================
// STEP > 1 WITH OFFSET ACCESS
// ============================================================================

// Variant 7: Step 2 with i+1 access
void variant7_step2_plus1() {
    int arr[20] = {0};
    for (int i = 0; i < 20; i += 2) {
        arr[i] = i;          // SAFE: i is 0,2,4,...,18
        arr[i + 1] = i + 1;  // SAFE: i+1 is 1,3,5,...,19
    }
}

// Variant 8: Step 2 with bound/2 iterations
void variant8_step2_half_bound() {
    int arr[100] = {0};
    for (int i = 0; i < 50; i += 2) {  // i goes 0,2,4,...,48
        arr[i] = i;
        arr[i + 1] = i + 1;  // SAFE: max i+1 = 49 < 100
    }
}

// Variant 9: Step 4 with multiple offsets
void variant9_step4_offsets() {
    int arr[100] = {0};
    for (int i = 0; i < 100; i += 4) {
        arr[i] = i;          // SAFE: i is 0,4,8,...,96
        arr[i + 1] = i + 1;  // SAFE: i+1 max is 97
        arr[i + 2] = i + 2;  // SAFE: i+2 max is 98
        arr[i + 3] = i + 3;  // SAFE: i+3 max is 99
    }
}

// Variant 10: Step equals array size (single iteration)
void variant10_step_equals_size() {
    int arr[10] = {0};
    for (int i = 0; i < 10; i += 10) {  // Only i=0
        arr[i] = 42;  // SAFE: only iteration has i=0
    }
}

// Variants 11-17 (min/ternary/computed bounds) moved to 712_loop_bound_gaps_xfail.cpp
// These require proof propagation from conditionals or function semantics

// ============================================================================
// MULTIPLE ARRAYS WITH RELATED BOUNDS
// ============================================================================

// Variant 18: Two arrays, same bound
void variant18_same_bound() {
    int src[20] = {0};
    int dst[20] = {0};
    for (int i = 0; i < 20; i++) {
        dst[i] = src[i];  // SAFE: both arrays have 20 elements
    }
}

// Variant 19: Different arrays, min bound
void variant19_min_of_arrays() {
    int small[10] = {0};
    int large[20] = {0};
    for (int i = 0; i < 10; i++) {  // Use smaller bound
        small[i] = i;
        large[i] = i * 2;  // SAFE: 10 < 20
    }
}

// Variant 20: Array with offset access
void variant20_offset_access() {
    int arr[30] = {0};
    for (int i = 0; i < 10; i++) {
        arr[i] = i;           // SAFE: i < 10 < 30
        arr[i + 10] = i + 10; // SAFE: i+10 < 20 < 30
        arr[i + 20] = i + 20; // SAFE: i+20 < 30
    }
}

// ============================================================================
// UNSIGNED ARITHMETIC EDGE CASES
// ============================================================================

// Variant 21: size_t loop variable
void variant21_size_t_loop() {
    int arr[100] = {0};
    for (size_t i = 0; i < 100; i++) {
        arr[i] = (int)i;  // SAFE: size_t, no wraparound in this range
    }
}

// Variant 22: uint32_t with explicit bound
void variant22_uint32_loop() {
    int arr[1000] = {0};
    for (uint32_t i = 0; i < 1000u; i++) {
        arr[i] = (int)i;  // SAFE: i < 1000
    }
}

// ============================================================================
// NESTED LOOPS WITH BOUNDS
// ============================================================================

// Variant 23: Row-major nested loop
void variant23_nested_row_major() {
    int matrix[10][20] = {{0}};
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 20; j++) {
            matrix[i][j] = i * 20 + j;  // SAFE: i<10, j<20
        }
    }
}

// Variant 24: Triangular nested loop
void variant24_triangular() {
    int arr[100] = {0};
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j <= i; j++) {  // j goes 0 to i
            arr[i * 10 + j] = j;  // SAFE: i<10, j<=i<10, so i*10+j < 100
        }
    }
}

// Variant 25: 3D nested loop
void variant25_3d_nested() {
    int cube[5][5][5] = {{{0}}};
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            for (int k = 0; k < 5; k++) {
                cube[i][j][k] = i + j + k;  // SAFE: all indices < 5
            }
        }
    }
}

int main() {
    variant1_constexpr_basic();
    variant6_template_param<10>();
    variant7_step2_plus1();
    variant18_same_bound();
    variant23_nested_row_major();
    return 0;
}
