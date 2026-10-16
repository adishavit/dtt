// 1750: Solver soundness - "may fail" is not "proven to fail"
//
// Each access below may or may not be in bounds. DTT cannot verify them, but
// none is a proven violation, so none may be an error in inform mode (the
// mode this test runs in, where "cannot verify" is a warning). Before the
// fix, a solver counterexample was reported as a proven violation.
#include <cstddef>
#include <vector>

int a[10];

[[clang::annotate("dtt:requires=forall(i, 0, n, arr[i] > 0)")]]
void requires_positive(const int *arr, size_t n);

// Variant 1: an unconstrained signed index.
int variant1_signed_index(int i) { return a[i]; }

// Variant 2: an unconstrained unsigned index.
int variant2_unsigned_index(unsigned i) { return a[i]; }

// Variant 3: a pointer offset by an unknown amount.
int variant3_pointer_offset(unsigned n) { return *(a + n); }

// Variant 4: a quantified requirement over unknown contents.
void variant4_forall_unknown(const int *p, size_t n) { requires_positive(p, n); }

// Variant 5: a loop bound against a different variable.
int variant5_loop_bound() {
    std::vector<int> v(10);
    int k = 5;
    int s = 0;
    for (int i = 0; i < k; ++i)
        s += v[i];
    return s;
}

int main() { return 0; }
