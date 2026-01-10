// Example 430: 2D array row pointer — XFAIL
// ============================================================================
// GAP: Row pointer from 2D array not tracked
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - p[10] should FAIL because row has only 10 elements
// - arr[i] for T arr[M][N] should track as N-element buffer
//
// CURRENT BEHAVIOR:
// - Compiles without error (row pointer not tracked)

void test_2d_array_row() {
    int arr[5][10];        // 5 rows of 10 ints
    int* p = arr[2];       // Points to row 2, should have _size=10

    p[10] = 1;             // GAP: Should FAIL (10 >= 10), but doesn't
}

int main() {
    test_2d_array_row();
    return 0;
}
