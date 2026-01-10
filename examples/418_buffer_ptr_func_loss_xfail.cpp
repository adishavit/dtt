// Example 418: Buffer pointer tracking loss at function call — XFAIL
// ============================================================================
// GAP: Buffer tracking not invalidated by unannotated function calls
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - unknown_func(p) should invalidate buffer tracking (Category C)
// - p[5] = 1 should WARN because tracking was lost
//
// CURRENT BEHAVIOR:
// - Compiles without error or warning
// - Buffer tracking persists incorrectly after function call
//
// This test will "fail" in ctest because WILL_FAIL expects compilation to fail,
// but it currently succeeds. This correctly indicates the gap.

void unknown_func(int* p);

void test_tracking_loss() {
    int arr[10];
    int* p = arr;          // p: buffer(_size=10)

    unknown_func(p);       // GAP: Should lose tracking, but doesn't

    p[5] = 1;              // GAP: Should WARN (tracking lost), but doesn't
}

int main() {
    test_tracking_loss();
    return 0;
}
