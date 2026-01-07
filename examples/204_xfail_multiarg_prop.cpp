// XFAIL: Multi-argument constraint propagation — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// When a function has a precondition relating two arguments (e.g., sizes must
// match), that relationship should be available inside the function body.
//
// WHY IT'S DIFFICULT:
// 1. Need to track relationships between variables, not just bounds
// 2. Equality constraints require unification-style reasoning
// 3. Transitive relationships: if a==b and b==c, then a==c
// 4. Must handle both equality and inequality relationships
//
// RATIONALE:
// Multi-argument constraints are common in APIs:
// - memcpy(dst, src, n): dst and src must both have size >= n
// - vector.assign(first, last): iterators must be from same container
// - matrix.multiply(a, b): a.cols == b.rows
//
// CURRENT BEHAVIOR:
// Multi-arg constraints are checked at call sites but not propagated
// into function bodies. Inside the function, we don't know that
// arg0._size == arg1._size even if the annotation says so.

struct Vec {
    int* data;
    int _size;
};

// Requires both vectors have same size
[[clang::annotate("dtt:requires=arg0._size == arg1._size")]]
void pairwiseOp(Vec& a, Vec& b);

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Vec& v);

// Inner function with same constraint
[[clang::annotate("dtt:requires=arg0._size == arg1._size")]]
void pairwiseHelper(Vec& x, Vec& y) {
    // Inside here, we know x._size == y._size

    // XFAIL: Can't propagate multi-arg constraint
    pairwiseOp(x, y);  // Should PASS: same precondition

    // Note: Even if we know x._size == y._size, we don't know
    // that either is > 0, so needsNonEmpty would correctly fail
}

int main() { return 0; }
