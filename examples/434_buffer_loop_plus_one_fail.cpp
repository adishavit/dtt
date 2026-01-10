// Example 434: Buffer access with i+1 in loop — should FAIL
// Tests that loop invariant i < n does NOT prove (i+1) < n
//
// When i = n-1 (last iteration), i+1 = n which is out of bounds.
// This is a real bug pattern that DTT should catch.

int get_size();

void test_loop_oob() {
    int n = get_size();
    int* arr = new int[n];

    for (int i = 0; i < n; ++i) {
        arr[i+1] = 42;     // FAIL: when i=n-1, accesses arr[n] (OOB)
    }

    delete[] arr;
}

int main() {
    test_loop_oob();
    return 0;
}
