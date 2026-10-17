// 1750: Solver soundness - what the facts prove stays proven
//
// Each access below is in bounds and DTT can prove it. Runs in enforce mode,
// so "cannot verify" fails the test.
#include <cstddef>
#include <vector>

int a[10];

// Variant 1: C division truncates toward zero: -1 / 2 == 0.
int variant1_negative_division(int x) {
    if (x > -2 && x < 0)
        return a[x / 2];
    return 0;
}

// Variant 2: a loop bound copied from the container size.
int variant2_loop_bound_alias(size_t n) {
    std::vector<int> v(n);
    size_t k = n;
    int s = 0;
    for (size_t i = 0; i < k; ++i)
        s += v[i];
    return s;
}

int main() { return 0; }
