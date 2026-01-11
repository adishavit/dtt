// Example 505: Direct array subscript with symbolic index — should FAIL
// ============================================================================
// Tests that DTT catches symbolic (runtime) index on constant-size arrays.
// arr[idx] should FAIL because idx < array_size not proved.

int get_index();

// Direct array subscript with symbolic index
void test_direct_array(int idx) {
    int arr[10];
    arr[idx] = 0;        // FAIL: idx < 10 not proved
}

int main(int argc, char**) {
    char buffer[8];
    buffer[argc] = '\0';  // FAIL: argc < 8 not proved
    return 0;
}
