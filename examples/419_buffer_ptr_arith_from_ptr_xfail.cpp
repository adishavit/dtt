// Example 419: Buffer pointer arithmetic from tracked pointer — XFAIL
// ============================================================================
// GAP: Pointer arithmetic from tracked pointer not bounds-checked
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - q + 6 should FAIL because it creates pointer at offset 11 (> 10)
// - DTT should check offset bounds when deriving from another pointer
//
// CURRENT BEHAVIOR:
// - Compiles without error
// - Only array + arithmetic is bounds-checked, not pointer + arithmetic
//
// This test will "fail" in ctest because WILL_FAIL expects compilation to fail,
// but it currently succeeds. This correctly indicates the gap.

void test_ptr_arith_bounds() {
    int arr[10];
    int* p = arr;          // p: buffer(_base=10, _offset=0)
    int* q = p + 5;        // q: buffer(_base=10, _offset=5)

    int* r = q + 6;        // GAP: Should FAIL (offset 11 > 10), but doesn't
}

int main() {
    test_ptr_arith_bounds();
    return 0;
}
