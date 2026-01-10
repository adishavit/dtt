// Example 501: Early return proof propagation — should COMPILE
// After `if (cond) { return; }`, the negation of cond should hold

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// Basic early return
void test_early_return(Vec& v) {
    if (v._size <= 0) {
        return;  // Exit if empty
    }
    // Here: _size > 0 must hold (negation of _size <= 0)
    v.front();  // OK
}

// Early return with less-than comparison
void test_early_return_lt(Vec& v) {
    if (v._size < 1) {
        return;
    }
    // Here: _size >= 1 holds, but requirement is > 0
    // NOTE: This requires >= 1 implying > 0 (not yet supported)
    // For now, use only <= 0 which negates to > 0 exactly
}

// Multiple early returns
void test_multiple_early_returns(Vec& v, bool flag) {
    if (v._size <= 0) {
        return;
    }
    if (flag) {
        return;  // Unrelated early return shouldn't affect proof
    }
    v.front();  // OK: _size > 0 still holds
}

int main() {
    Vec v;
    v._size = 5;
    test_early_return(v);
    test_early_return_lt(v);
    test_multiple_early_returns(v, false);
    return 0;
}
