// 707_loop_bound_tracking_pass.cpp
// Tests for loop bound tracking - DTT should recognize loop invariants
// and not warn about array access within proven loop bounds

#include <cstddef>
#include <cstdint>
#include <cstring>

// ============================================================================
// BASIC FOR LOOP PATTERNS
// ============================================================================

// Variant 1: Simple for loop with < bound
void variant1_simple_for_lt() {
    int arr[10] = {0};
    for (int i = 0; i < 10; i++) {
        arr[i] = i;  // SAFE: i is 0-9 due to loop condition
    }
}

// Variant 2: For loop with <= bound
void variant2_for_le() {
    int arr[11] = {0};
    for (int i = 0; i <= 10; i++) {
        arr[i] = i;  // SAFE: i is 0-10 due to loop condition
    }
}

// Variant 3: For loop with size_t
void variant3_for_size_t() {
    double arr[100] = {0};
    for (size_t i = 0; i < 100; i++) {
        arr[i] = (double)i;  // SAFE: i is 0-99
    }
}

// Variant 4: For loop with constant bound
constexpr int SIZE = 50;
void variant4_for_constexpr() {
    int arr[SIZE] = {0};
    for (int i = 0; i < SIZE; i++) {
        arr[i] = i * 2;  // SAFE: i < SIZE and arr has SIZE elements
    }
}

// Variant 5: For loop counting down
void variant5_for_countdown() {
    int arr[10] = {0};
    for (int i = 9; i >= 0; i--) {
        arr[i] = i;  // SAFE: i is 9 down to 0
    }
}

// Variant 6: Nested loops
void variant6_nested_loops() {
    int matrix[5][5] = {{0}};
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            matrix[i][j] = i * 5 + j;  // SAFE: both i and j are 0-4
        }
    }
}

// Variant 7: Loop with computed bound from array
void variant7_computed_bound() {
    int arr[20] = {0};
    int n = 20;  // Could be computed
    for (int i = 0; i < n; i++) {
        arr[i] = i;  // SAFE if DTT tracks n == 20
    }
}

// ============================================================================
// WHILE LOOP PATTERNS
// ============================================================================

// Variant 8: While loop with counter
void variant8_while_counter() {
    int arr[10] = {0};
    int i = 0;
    while (i < 10) {
        arr[i] = i;  // SAFE: while condition ensures i < 10
        i++;
    }
}

// Variant 9: While loop with decrement
void variant9_while_decrement() {
    int arr[10] = {0};
    int i = 9;
    while (i >= 0) {
        arr[i] = i;  // SAFE: i is 9 down to 0
        i--;
    }
}

// Variant 10: Do-while loop
void variant10_do_while() {
    int arr[10] = {0};
    int i = 0;
    do {
        arr[i] = i;  // Need to ensure i < 10 before access
        i++;
    } while (i < 10);
}

// ============================================================================
// LOOP WITH SIZEOF
// ============================================================================

// Variant 11: Loop with sizeof array
void variant11_sizeof_array() {
    int arr[25] = {0};
    for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++) {
        arr[i] = (int)i;  // SAFE: sizeof ensures correct bound
    }
}

// Variant 12: Loop with std::size (C++17)
void variant12_template_size() {
    int arr[30] = {0};
    constexpr size_t len = sizeof(arr) / sizeof(arr[0]);
    for (size_t i = 0; i < len; i++) {
        arr[i] = (int)i;  // SAFE: len == 30
    }
}

// ============================================================================
// LOOP WITH POINTER BOUNDS
// ============================================================================

// Variant 13: Pointer iteration with end pointer
void variant13_pointer_iteration() {
    int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int* begin = arr;
    int* end = arr + 10;

    for (int* p = begin; p < end; p++) {
        *p = *p * 2;  // SAFE: p is within [begin, end)
    }
}

// Variant 14: Pointer with index computation
void variant14_pointer_index() {
    int arr[10] = {0};
    int* ptr = arr;
    for (int i = 0; i < 10; i++) {
        ptr[i] = i;  // SAFE: equivalent to arr[i] with i < 10
    }
}

// ============================================================================
// LOOP WITH EARLY EXIT
// ============================================================================

// Variant 15: Loop with break
void variant15_loop_break() {
    int arr[100] = {0};
    for (int i = 0; i < 100; i++) {
        arr[i] = i;  // SAFE: i < 100
        if (i == 50) break;
    }
}

// Variant 16: Loop with continue
void variant16_loop_continue() {
    int arr[20] = {0};
    for (int i = 0; i < 20; i++) {
        if (i % 2 == 0) continue;
        arr[i] = i;  // SAFE: i still < 20 after continue
    }
}

// Variant 17: Loop with return
int variant17_loop_return() {
    int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (int i = 0; i < 10; i++) {
        if (arr[i] == 5) return i;  // SAFE: i < 10
    }
    return -1;
}

// ============================================================================
// MULTIPLE ARRAY ACCESS IN LOOP
// ============================================================================

// Variant 18: Two arrays same bound
void variant18_two_arrays() {
    int src[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int dst[10] = {0};
    for (int i = 0; i < 10; i++) {
        dst[i] = src[i];  // SAFE: both arrays have 10 elements
    }
}

// Variant 19: Array and computed index
void variant19_computed_index() {
    int arr[20] = {0};
    for (int i = 0; i < 10; i++) {
        arr[i] = i;           // SAFE: i < 10 < 20
        arr[i + 10] = i + 10; // SAFE: i + 10 < 20
    }
}

// Variant 20: Adjacent element access
void variant20_adjacent() {
    int arr[10] = {0};
    for (int i = 0; i < 9; i++) {  // Note: < 9, not < 10
        arr[i] = i;
        arr[i + 1] = i + 1;  // SAFE: i+1 <= 9 < 10
    }
}

// ============================================================================
// RANGE-BASED PATTERNS
// ============================================================================

// Variant 21: Simulated range-based for (index version)
void variant21_range_simulation() {
    int arr[5] = {10, 20, 30, 40, 50};
    int sum = 0;
    for (int idx = 0, end = 5; idx < end; idx++) {
        sum += arr[idx];  // SAFE: idx < end == 5
    }
    (void)sum;
}

// ============================================================================
// COMPLEX LOOP PATTERNS
// ============================================================================

// Variant 22: Loop with multiple conditions
void variant22_multiple_conditions() {
    int arr[100] = {0};
    int max = 50;
    for (int i = 0; i < 100 && i < max; i++) {
        arr[i] = i;  // SAFE: i < min(100, max) = 50
    }
}

// Variant 23: Loop with pre-checked bound
void variant23_pre_checked() {
    int arr[10] = {0};
    int n = 10;
    if (n > 10) return;  // Ensures n <= 10
    for (int i = 0; i < n; i++) {
        arr[i] = i;  // SAFE: n <= 10 guaranteed
    }
}

// Variants 24-25 moved to 712_loop_bound_gaps_xfail.cpp (strlen/user-defined min)

// ============================================================================
// EDGE CASES
// ============================================================================

// Variant 26: Empty loop (bound is 0)
void variant26_empty_loop() {
    int arr[10] = {0};
    int n = 0;
    for (int i = 0; i < n; i++) {
        arr[i] = i;  // Never executed, but would be safe if it were
    }
}

// Variant 27: Single iteration
void variant27_single_iteration() {
    int arr[1] = {0};
    for (int i = 0; i < 1; i++) {
        arr[i] = 42;  // SAFE: only i=0
    }
}

// Variant 28: Loop variable used after loop
void variant28_post_loop() {
    int arr[10] = {0};
    int i;
    for (i = 0; i < 10; i++) {
        arr[i] = i;  // SAFE: i < 10
    }
    // After loop, i == 10, so arr[i] would be OOB
    // Don't access arr[i] here!
}

// Variant 29: Loop with step > 1
void variant29_step_2() {
    int arr[20] = {0};
    for (int i = 0; i < 20; i += 2) {
        arr[i] = i;      // SAFE: i is 0, 2, 4, ..., 18
        arr[i + 1] = i + 1;  // SAFE: i+1 is 1, 3, 5, ..., 19
    }
}

// Variant 30: Loop with unsigned wraparound awareness
void variant30_unsigned_aware() {
    int arr[10] = {0};
    for (unsigned int i = 0; i < 10u; i++) {
        arr[i] = (int)i;  // SAFE: unsigned, no wraparound issues here
    }
}

int main() {
    // All these patterns should compile without DTT warnings
    // because the loop bounds guarantee safe array access
    variant1_simple_for_lt();
    variant8_while_counter();
    variant11_sizeof_array();
    return 0;
}
