// Example 416: Buffer pointer copy assignment — should FAIL
// Tests that q = p copies buffer tracking and catches OOB

void test_pointer_copy() {
    int arr[10];
    int* p = arr;      // p: buffer(_size=10)
    int* q;
    q = p;             // q should inherit buffer(_size=10)

    q[9] = 1;          // Should be OK: 9 < 10
    q[10] = 1;         // Should FAIL: 10 >= 10  dtt-error {{out of bounds}}
}

int main() {
    test_pointer_copy();
    return 0;
}
