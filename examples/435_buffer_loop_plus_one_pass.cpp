// Example 435: Buffer access with i+1 in loop (safe) — should COMPILE
// Tests that loop invariant i < limit with limit = n-1 proves (i+1) < n
//
// Using a helper variable allows Z3 to track the relationship:
// - limit = n - 1
// - i < limit (loop invariant)
// - Therefore i + 1 <= limit < n (safe)

int get_size();

void test_loop_safe() {
    int n = get_size();
    int* arr = new int[n];

    int limit = n - 1;                  // Helper variable
    for (int i = 0; i < limit; ++i) {
        arr[i+1] = 42;     // OK: i < limit = n-1, so i+1 < n
    }

    delete[] arr;
}

int main() {
    test_loop_safe();
    return 0;
}
