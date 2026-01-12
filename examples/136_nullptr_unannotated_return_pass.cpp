// Example 136: Unannotated function return — EXPECTED TO FAIL (not yet implemented)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests whether DTT can infer nullability from unannotated functions
// by analyzing their implementation (inter-procedural analysis).
//
// ============================================================================
// WHY THIS IS DIFFICULT
// ============================================================================
// DTT is a SINGLE-PASS, FUNCTION-SCOPED analyzer:
//
//     bool TraverseFunctionDecl(FunctionDecl *F) {
//         Env.clear();  // <-- Environment cleared per function!
//         ...
//     }
//
// When analyzing main(), DTT doesn't know what createData() returns because:
// 1. createData() may be defined AFTER main() in the source file
// 2. createData() may be in a different translation unit (.cpp file)
// 3. Even if visible, analyzing it requires INTER-PROCEDURAL analysis
//
// Inter-procedural analysis requires:
// - Building a call graph
// - Analyzing functions in dependency order
// - Propagating return value properties across function boundaries
// - Handling recursion and mutual recursion
//
// ============================================================================
// RATIONALE FOR NOT IMPLEMENTING
// ============================================================================
// From our discussion:
// - DTT is designed for ANNOTATED contracts, not automatic inference
// - Whole-program analysis is expensive and doesn't fit plugin model
// - The SOLUTION is to add annotations: dtt:ensures=result._nonnull = 1
//
// This keeps DTT simple and predictable — you annotate what you want tracked.
//
// ============================================================================
// WORKAROUND
// ============================================================================
// Add an annotation to createData():
//
//     [[clang::annotate("dtt:ensures=result._nonnull = 1")]]
//     Data* createData() { return new Data(); }
//
// Then the caller can use the return value safely.
// See example 119_nullptr_annotated_pass.cpp for working example.
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT reports "nullability unknown" because it can't see inside createData().
// This is CORRECT conservative behavior — without annotation, we don't know.
//
// ============================================================================

struct Data {
    int value;
};

// No annotation — DTT can't know this always returns non-null
Data* createData() {
    return new Data();  // Always returns non-null, but DTT doesn't analyze this
}

int main() {
    Data* ptr = createData();  // DTT doesn't know createData() returns non-null

    // Even though createData() clearly returns `new Data()` which is never null,
    // DTT can't track this without annotations

    ptr->value = 42;  // XFAIL: DTT reports unknown nullability
                      // Workaround: add dtt:ensures annotation to createData()

    return 0;
}
