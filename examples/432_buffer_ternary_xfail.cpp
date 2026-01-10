// Example 432: Ternary operator with arrays — XFAIL
// ============================================================================
// GAP: Ternary operator loses buffer tracking
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - p[6] should FAIL because arr2 has only 5 elements
// - Ternary should track minimum size or produce warning
//
// CURRENT BEHAVIOR:
// - Compiles without error (ternary loses tracking)

void test_ternary(bool b) {
    int arr1[10];
    int arr2[5];
    int* p = b ? arr1 : arr2;  // Should track min(_size)=5 or warn

    p[6] = 1;                   // GAP: Should FAIL (6 >= 5 for arr2), but doesn't
}

int main() {
    test_ternary(true);
    return 0;
}
