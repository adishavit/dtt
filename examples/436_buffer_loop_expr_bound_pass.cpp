// Example 436: Loop with expression bound (i < n - 1) — should COMPILE
// Tests that binary expressions in loop bounds are tracked via synthetic variables
//
// The loop condition `i < n - 1` is converted to a synthetic variable:
//   __loop_bound_X = n - 1
//   VarBoundProof: i < __loop_bound_X
//
// Z3 then proves: i < n-1, so i+1 <= n-1 < n (safe)

int get_size();

void test_expr_bound() {
    int n = get_size();
    int* arr = new int[n];

    for (int i = 0; i < n - 1; ++i) {
        arr[i+1] = 42;     // OK: i < n-1, so i+1 < n
    }

    delete[] arr;
}

int main() {
    test_expr_bound();
    return 0;
}
