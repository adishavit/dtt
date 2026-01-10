// Example 479: Pointer reassignment tracking — XFAIL
// ============================================================================
// GAP: Pointer reassignment (p = p + n) loses buffer tracking
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - p = p + n should update offset by n
// - p += n should update offset by n
// - Access past end should FAIL
//
// CURRENT BEHAVIOR:
// - Tracking is lost entirely (pointer becomes unknown)

void test_reassign_oob() {
    int arr[5];
    int* p = arr;   // offset=0, size=5
    p = p + 10;     // GAP: should be offset=10, size=-5 (OOB)
    *p = 0;         // SHOULD FAIL: way past end
}

void test_compound_assign_oob() {
    int arr[5];
    int* p = arr;
    p += 10;        // GAP: should update offset
    *p = 0;         // SHOULD FAIL: way past end
}

void test_reassign_valid() {
    int arr[10];
    int* p = arr;
    p = p + 5;      // Should track: offset=5, size=5
    p[0] = 0;       // SHOULD be OK
    p[4] = 0;       // SHOULD be OK
    p[5] = 0;       // SHOULD FAIL
}

int main() {
    test_reassign_oob();
    test_compound_assign_oob();
    test_reassign_valid();
    return 0;
}
