// Example 433: Buffer access in loop — should COMPILE
// Tests that loop invariants (i < n) are captured and used for bounds checking
//
// In a for-loop `for (int i = 0; i < n; i++)`, the condition `i < n`
// establishes that inside the loop body, i is always less than n.
// DTT captures this as a VarBoundProof and Z3 uses it to prove bounds.

int get_size();

void test_loop_invariant() {
    int n = get_size();
    int* p = new int[n];   // p: _size = n

    for (int i = 0; i < n; i++) {
        p[i] = i;          // OK: loop invariant i < n proves i < _size
    }

    delete[] p;
}

int main() {
    test_loop_invariant();
    return 0;
}
