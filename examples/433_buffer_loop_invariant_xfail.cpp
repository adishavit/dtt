// Example 433: Buffer access in loop — should COMPILE (xfail)
// GAP: Loop invariants not yet captured
//
// In a for-loop `for (int i = 0; i < n; i++)`, the condition `i < n`
// establishes that inside the loop body, i is always less than n.
// This requires capturing the loop condition as a constraint on the
// loop variable, which is more complex than simple if-guards.
//
// Current behavior: Warns at p[i] because i < n not proven
// Expected behavior: No warning since loop invariant proves bounds

int get_size();

void test_loop_invariant() {
    int n = get_size();
    int* p = new int[n];   // p: _size = n

    for (int i = 0; i < n; i++) {
        p[i] = i;          // GAP: Should be OK (i < n), but warns
    }

    delete[] p;
}

int main() {
    test_loop_invariant();
    return 0;
}
