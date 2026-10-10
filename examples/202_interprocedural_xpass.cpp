// Known false positive: a declared inequality postcondition on an output
// argument is not propagated to the subsequent precondition check.

struct Vec {
    int* data;
    int _size;
};

// Initializes vector with at least n elements
[[clang::annotate("dtt:ensures=arg0._size >= arg1")]]
void initVec(Vec& v, int n);

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Vec& v);

int main() {
    Vec v{};
    // Start from a known size; only the declared postcondition can establish >= 10.

    initVec(v, 10);  // Postcondition: v._size >= 10

    // Known false positive: the inequality postcondition is not established.
    needsNonEmpty(v);  // Should PASS (initVec guarantees _size >= 10) but FAILS

    return 0;
}
