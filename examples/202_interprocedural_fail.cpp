// Postcondition propagation to caller — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// When a function with dtt:ensures is called, the postconditions should
// be applied to the objects at the call site, allowing subsequent checks.
//
// WHY IT'S DIFFICULT:
// 1. Need to map postcondition variables to actual arguments
// 2. @old() references need snapshot before call
// 3. Must handle return values, output params, and modified objects
// 4. Expressions in postconditions need substitution
//
// RATIONALE:
// Without postcondition propagation to callers:
// - Can't chain operations: init() then use() fails
// - Must re-establish proofs after every call
// - Factory patterns don't work without workarounds
//
// CURRENT BEHAVIOR:
// dtt:ensures is checked INSIDE the function at return statements.
// The postconditions are NOT applied to the caller's view of objects.

struct Vec {
    int* data;
    int _size;
};

// Initializes vector with at least n elements
[[clang::annotate("dtt:ensures=_size >= arg0")]]
void initVec(Vec& v, int n);

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Vec& v);

int main() {
    Vec v;
    // v._size is unknown

    initVec(v, 10);  // Postcondition: v._size >= 10

    // XFAIL: v._size is still unknown - postcondition not applied to caller
    needsNonEmpty(v);  // Should PASS (initVec guarantees _size >= 10) but FAILS

    return 0;
}
