// XFAIL: OR constraint propagation — currently NOT IMPLEMENTED
//
// WHAT IS TESTED:
// Disjunctive preconditions like (A || B) should be propagated into function
// bodies. If we know (A || B) at entry, we should be able to use that fact
// to satisfy calls that require A OR require B (with case analysis).
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
// OR constraints are parsed but SKIPPED in contract propagation.
// Inside a function with OR precondition, we have NO proof of either disjunct.
// This test demonstrates the limitation.
//
// RELATED LIMITATION:
// OR constraints are also NOT ENFORCED at call sites (callers can call
// without proving either disjunct). A separate test could document that,
// but it would compile clean and not fit the WILL_FAIL test pattern.

struct Container {
    int* data;
    int _size;
    int _capacity;
};

// Only requires one of the disjuncts
[[clang::annotate("dtt:requires=_size > 0")]]
void needsSize(Container& c);

[[clang::annotate("dtt:requires=_capacity > 0")]]
void needsCapacity(Container& c);

// This function has OR precondition - inside, we SHOULD know (size>0 || cap>0)
// With proper OR propagation, we could use case analysis to prove nested calls
[[clang::annotate("dtt:requires=(_size > 0 || _capacity > 0)")]]
void processContainer(Container& c) {
    // If OR constraints were propagated, we'd have proof of the disjunction.
    // We could then reason: "in the _size>0 case, needsSize is satisfied"
    // For now, calling needsSize fails because we have NO proof of _size > 0

    // XFAIL: This SHOULD pass (we know _size>0 || _capacity>0, and this is
    // the _size>0 branch of that disjunction) but FAILS because OR isn't propagated
    needsSize(c);  // Currently: ERROR (no proof of _size > 0)
}

int main() {
    Container c;
    c._size = 10;
    c._capacity = 0;  // Only size is set, but that satisfies the OR
    processContainer(c);
    return 0;
}
