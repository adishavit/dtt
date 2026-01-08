// Example 143: Pointer nullability from complex sources — EXPECTED TO FAIL
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// DTT tracks pointer nullability for simple cases:
//   Data* p1 = new Data();  // p1._nonnull = 1 (new expression)
//   Data* p2 = p1;          // p2._nonnull = 1 (simple variable copy)
//   Data* p3 = &local;      // p3._nonnull = 1 (address-of)
//
// But DTT loses tracking for COMPLEX pointer sources:
//   Data* p = container.ptr;    // MemberExpr - NOT tracked
//   Data* p = getPointer();     // CallExpr - NOT tracked (unless annotated)
//   Data* p = arr[i];           // ArraySubscriptExpr - NOT tracked
//
// ============================================================================
// WHY THIS IS DIFFICULT
// ============================================================================
// The code in VisitVarDecl (lines 1812-1833) and VisitBinaryOperator
// (lines 1959-1981) only handles:
//
//   1. inferNonnullFromExpr() - new expressions, address-of
//   2. DeclRefExpr - simple variable references like p1
//
// For anything else, it falls through to:
//   setField(var, "_nonnull", SymValue::Unknown());
//
// To fix this, DTT would need to:
//   1. Handle MemberExpr: getField(basePath + "." + memberName, "_nonnull")
//   2. Handle CallExpr: check for annotated returns
//   3. Handle ArraySubscriptExpr: complex, depends on element tracking
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT marks the pointer as UNKNOWN and requires explicit null checks.
// This is SAFE (no false negatives) but ANNOYING (false positives).
//
// ============================================================================

struct Data {
    int value;
};

struct Container {
    Data* ptr;
};

int main() {
    // CASE 1: Copy from member pointer
    // c.ptr is non-null, but DTT doesn't propagate this to p1
    Container c;
    c.ptr = new Data();     // c.ptr._nonnull = 1
    Data* p1 = c.ptr;       // XFAIL: p1._nonnull = UNKNOWN (MemberExpr not handled)
    p1->value = 10;         // ERROR: DTT reports unknown nullability

    // CASE 2: Assignment from member pointer (same issue)
    Data* p2 = nullptr;
    p2 = c.ptr;             // XFAIL: p2._nonnull = UNKNOWN
    p2->value = 20;         // ERROR: DTT reports unknown nullability

    // CASE 3: Chained member access
    // Even though we just set c.ptr, reading it back loses tracking
    c.ptr->value = 30;      // This might work (direct MemberExpr on c.ptr)
    Data* p3 = c.ptr;       // But storing to variable loses it
    p3->value = 40;         // ERROR: p3 nullability unknown

    return 0;
}
