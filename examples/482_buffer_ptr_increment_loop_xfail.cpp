// Example 482: Pointer increment in loop — XFAIL
// ============================================================================
// GAP: Pointer increment in loops doesn't track cumulative offset
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - Track that offset = initial + loop_variable
// - Detect when offset can exceed buffer bounds over iterations
//
// CURRENT BEHAVIOR:
// - Loop body analyzed once with initial offset
// - p++ updates offset, but not re-analyzed for subsequent iterations
// - OOB not caught

void test_loop_oob() {
    int arr[5];
    int* p = arr;
    for (int i = 0; i < 10; i++) {  // Loop exceeds array!
        *p = i;  // SHOULD FAIL: when i >= 5, offset >= 5
        p++;
    }
}

void test_loop_with_deref_after() {
    int arr[3];
    int* p = arr;
    for (int i = 0; i < 3; i++) {
        p++;     // After loop: offset = 3
    }
    *p = 0;      // SHOULD FAIL: offset 3 >= size 3
                 // (This case might work if loop is fully unrolled)
}

int main() {
    test_loop_oob();
    test_loop_with_deref_after();
    return 0;
}
