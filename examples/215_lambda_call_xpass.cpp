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
// A call to a lambda invalidates what it captures by reference
// (1763_lambda_capture_*), so needsEmpty(v) is "cannot verify": the body's
// specific effect (v.len = 0) is not replayed at the call.
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

    // XPASS: After call, v.len is 0, but DTT only knows it is unknown
    needsEmpty(v);  // FAILS: v.len should be 0 but DTT doesn't know

    return;
}

int main() { test(); return 0; }
