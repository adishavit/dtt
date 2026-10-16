// Example 417: Buffer pointer symbolic bounds warning — should FAIL
// ============================================================================
// When accessing a buffer with symbolic size and no guard proving bounds,
// DTT emits a Category D warning: bounds can't be verified.
//
// p[0] should FAIL because n > 0 not proved. The size n comes from
// an unknown function - it could be 0.

#include <cstddef>

size_t get_size();

void test_symbolic_warning() {
    size_t n = get_size();
    int* p = new int[n];   // _base_length = n (symbolic)

    p[0] = 0;              // FAIL: n > 0 not proved (Category D)  dtt-error {{out of bounds|cannot verify}}

    delete[] p;
}

int main() {
    test_symbolic_warning();
    return 0;
}
