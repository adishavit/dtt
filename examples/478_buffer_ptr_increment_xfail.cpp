// Example 478: Pointer increment tracking — XFAIL
// ============================================================================
// GAP: Pointer increment (p++) doesn't update tracked offset
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - p++ should increment the tracked offset
// - *p after p++ past array end should FAIL
//
// CURRENT BEHAVIOR:
// - Compiles without error (offset not updated)

void test_increment_oob() {
    int arr[3];
    int* p = arr;  // offset=0, size=3
    p++;           // GAP: offset should become 1
    p++;           // GAP: offset should become 2
    p++;           // GAP: offset should become 3 (past end!)
    *p = 0;        // SHOULD FAIL: offset 3 >= size 3
}

void test_increment_loop_oob() {
    int arr[5];
    int* p = arr;
    for (int i = 0; i < 10; i++) {  // Loop exceeds array!
        *p = i;  // SHOULD FAIL when i >= 5
        p++;
    }
}

int main() {
    test_increment_oob();
    test_increment_loop_oob();
    return 0;
}
