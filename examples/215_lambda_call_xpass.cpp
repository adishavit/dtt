// Lambda call tracking — not yet implemented
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a lambda is CALLED, its effects on captured variables should be tracked.
// Currently, DTT correctly isolates lambda DEFINITIONS (see 101_lambda_timing_pass)
// but doesn't track the effects when lambdas are INVOKED.
//
// ============================================================================
// WHY IT'S DIFFICULT
// ============================================================================
// Lambda call tracking requires:
// 1. Associating lambda body with the lambda variable
// 2. When lambda is called, "replay" its body's effects on captured variables
// 3. Handle mutable vs const captures
// 4. Track multiple calls (effects compound)
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// Lambda calls are treated as unknown function calls.
// Captured variables may be invalidated conservatively, but the specific
// effects (like v.len = 0) are not tracked.
//
// GUIDE.md line 423 notes: "Lambda call tracking not yet implemented"
//
// ============================================================================

struct Vec {
    int len;
};

[[clang::annotate("dtt:requires=len > 0")]]
void needsNonEmpty(Vec& v);

[[clang::annotate("dtt:requires=len == 0")]]
void needsEmpty(Vec& v);

void test() {
    Vec v;
    v.len = 10;  // v.len = 10

    // Lambda that modifies captured variable
    auto setZero = [&v]() {
        v.len = 0;
    };

    // Before call: v.len = 10
    needsNonEmpty(v);  // Should PASS: v.len = 10

    // Call the lambda — this modifies v.len to 0
    setZero();

    // XFAIL: After call, v.len should be 0, but DTT doesn't track this
    // DTT either thinks v.len is still 10, or marks it as unknown
    needsEmpty(v);  // FAILS: v.len should be 0 but DTT doesn't know

    return;
}

int main() { test(); return 0; }
