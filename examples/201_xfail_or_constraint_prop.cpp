// XFAIL: OR constraint support — currently NOT IMPLEMENTED
//
// WHAT IS TESTED:
// Disjunctive preconditions like (A || B) should be:
// 1. Checked at call sites (caller must prove A or B holds)
// 2. Propagated into function bodies as assumptions
//
// WHY IT'S DIFFICULT:
// 1. Disjunctions require tracking multiple possible states simultaneously
// 2. Need case-split reasoning: "if A holds, then X; if B holds, then Y"
// 3. Proof checking becomes NP-complete with arbitrary disjunctions
// 4. Memory overhead: 2^N states for N disjuncts
//
// RATIONALE:
// Real APIs often have disjunctive preconditions:
// - "buffer is valid OR size is zero"
// - "iterator is end OR points to valid element"
// - "optional has value OR is explicitly empty"
//
// CURRENT BEHAVIOR:
// OR constraints are parsed but NOT ENFORCED at call sites.
// They are also skipped in contract propagation.
// This test demonstrates both limitations.

struct Container {
    int* data;
    int _size;
    int _capacity;
};

// Requires: container has space OR has allocated storage
[[clang::annotate("dtt:requires=(_size > 0 || _capacity > 0)")]]
void processContainer(Container& c);

// Only requires one of the disjuncts
[[clang::annotate("dtt:requires=_size > 0")]]
void needsSize(Container& c);

[[clang::annotate("dtt:requires=_capacity > 0")]]
void needsCapacity(Container& c);

int main() {
    Container c;
    c._size = 0;
    c._capacity = 0;  // Both are 0, violates (_size > 0 || _capacity > 0)

    // XFAIL: This SHOULD fail but doesn't because OR constraints aren't checked
    processContainer(c);  // Should ERROR but currently compiles

    return 0;
}
