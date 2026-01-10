// Example 431: Struct member array — XFAIL
// ============================================================================
// GAP: Array member of struct not tracked
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - p[10] should FAIL because s.data has only 10 elements
// - s.member for struct { T member[N]; } should track as N-element buffer
//
// CURRENT BEHAVIOR:
// - Compiles without error (struct member array not tracked)

struct Buffer {
    int data[10];
};

void test_struct_member_array() {
    Buffer buf;
    int* p = buf.data;     // Should have _size=10

    p[10] = 1;             // GAP: Should FAIL (10 >= 10), but doesn't
}

int main() {
    test_struct_member_array();
    return 0;
}
