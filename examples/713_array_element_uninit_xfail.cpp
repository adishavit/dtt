// 713_array_element_uninit_xfail.cpp
// GAP: DTT doesn't check array element reads for uninitialized scalars
// Reading arr[i] from an uninitialized array should be caught

// =============================================================================
// Simple array element reads
// =============================================================================

// Variant 1: Read first element
int variant1_first_element() {
    int arr[10];
    return arr[0];  // GAP: arr[0] is uninitialized
}

// Variant 2: Read arbitrary element
int variant2_arbitrary_element() {
    int arr[10];
    return arr[5];  // GAP: arr[5] is uninitialized
}

// Variant 3: Read with variable index
int variant3_variable_index(int i) {
    if (i < 0 || i >= 10) return 0;
    int arr[10];
    return arr[i];  // GAP: arr[i] is uninitialized
}

// Variant 4: Array element in expression
int variant4_element_expr() {
    int arr[5];
    return arr[0] + arr[1] * 2;  // GAP: both elements uninitialized
}

// Variant 5: Array element in condition
int variant5_element_condition() {
    int arr[5];
    if (arr[0] > 0) {  // GAP: arr[0] is uninitialized
        return 1;
    }
    return 0;
}

// =============================================================================
// Partial initialization patterns
// =============================================================================

// Variant 6: Partial init, read uninit element
int variant6_partial_init() {
    int arr[10];
    arr[0] = 1;
    arr[1] = 2;
    return arr[5];  // GAP: arr[5] is still uninitialized
}

// Variant 7: Loop partial init
int variant7_loop_partial() {
    int arr[10];
    for (int i = 0; i < 5; i++) {
        arr[i] = i;
    }
    return arr[7];  // GAP: arr[7] not initialized by loop
}

// =============================================================================
// Multi-dimensional arrays
// =============================================================================

// Variant 8: 2D array element
int variant8_2d_array() {
    int arr[3][3];
    return arr[1][1];  // GAP: arr[1][1] is uninitialized
}

// Variant 9: 2D partial init
int variant9_2d_partial() {
    int arr[3][3];
    arr[0][0] = 1;
    return arr[2][2];  // GAP: arr[2][2] is uninitialized
}

// =============================================================================
// Different types
// =============================================================================

// Variant 10: Double array
double variant10_double_array() {
    double arr[5];
    return arr[0];  // GAP: arr[0] is uninitialized
}

// Variant 11: Bool array
bool variant11_bool_array() {
    bool arr[5];
    return arr[0];  // GAP: arr[0] is uninitialized
}

// =============================================================================
// Safe patterns (should NOT trigger when gap is fixed)
// =============================================================================

// Safe 1: Value-initialized array
int safe1_value_init() {
    int arr[10] = {};
    return arr[5];  // OK: value-initialized to 0
}

// Safe 2: Fully initialized array
int safe2_full_init() {
    int arr[3] = {1, 2, 3};
    return arr[1];  // OK: initialized
}

// Safe 3: Element assigned before read
int safe3_assign_before_read() {
    int arr[10];
    arr[5] = 42;
    return arr[5];  // OK: assigned before read
}

// Safe 4: Loop full init
int safe4_loop_full_init() {
    int arr[10];
    for (int i = 0; i < 10; i++) {
        arr[i] = i;
    }
    return arr[5];  // OK: all elements initialized
}

int main() {
    return 0;
}
