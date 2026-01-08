// XFAIL: Forwarding reference handling — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// Template functions with forwarding references (T&&) should only
// invalidate when the argument is actually moved (rvalue passed).
// Lvalue arguments should NOT be invalidated.
//
// WHY IT'S DIFFICULT:
// 1. T&& in templates is a forwarding reference, not rvalue reference
// 2. Behavior depends on the VALUE CATEGORY of the argument at call site
// 3. When passed an lvalue, T&& deduces to T& (reference collapsing)
// 4. Would need to track value category through template instantiation
//
// RATIONALE:
// Forwarding references are fundamental to generic programming:
// - std::forward, std::move semantics
// - Perfect forwarding in wrapper functions
// - Emplace operations that forward to constructors
//
// CURRENT BEHAVIOR:
// DTT treats all T&& parameters as rvalue refs and invalidates.
// This causes false positives when passing lvalues to forwarding functions.

struct Vec {
    int* data;
    int _size;
};

// Forwarding reference - should only invalidate for rvalues
template<typename T>
void maybe_consume(T&& v);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v);

void test() {
    Vec v;
    v._size = 10;

    maybe_consume(v);  // Passing LVALUE - T&& deduces to Vec&, not Vec&&

    // XFAIL: v._size is invalidated even though lvalue was passed
    process(v);  // Should PASS: lvalue wasn't moved
}

int main() { test(); return 0; }
