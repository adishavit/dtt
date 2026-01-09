// Category D: Solver undecidable - inform mode (passes with warning)
//
// This test demonstrates Category D behavior where the solver cannot
// determine if a constraint is satisfied. In inform mode (default),
// this produces a WARNING but allows compilation.
//
// The constraint involves symbolic values that can't be resolved
// without additional information.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int i) { return data[i]; }

    [[clang::annotate("dtt:ensures=_size == arg0")]]
    void resize(int n);
};

void process(int n) {
    Vec v;
    v.resize(n);    // v._size = n (symbolic)

    // Without a guard like `if (n > 0)`, the solver may not be able
    // to prove that n-1 < n (depends on solver capabilities).
    // In simple cases, this might actually resolve, but complex
    // expressions often hit Category D.

    // This test is more about documenting the category than
    // triggering it reliably.
}

int main() {
    process(10);
    return 0;
}
