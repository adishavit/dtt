// Example 144: Pointer initialization from indirect sources — EXPECTED TO FAIL
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests pointer nullability tracking when the source is:
//   - A ternary expression (even when both branches are non-null)
//   - An array element
//   - A dereferenced pointer-to-pointer
//
// These are separate from 143 (member pointers) and 136 (function returns).
//
// ============================================================================
// WHY THIS IS DIFFICULT
// ============================================================================
// DTT's inferNonnullFromExpr only recognizes:
//   - CXXNewExpr (new T())
//   - UnaryOperator with UO_AddrOf (&x)
//   - CXXThisExpr (this)
//   - IntegerLiteral 0 / CXXNullPtrLiteralExpr (nullptr)
//
// Everything else falls through to UNKNOWN.
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT cannot determine nullability and requires explicit guards.
//
// ============================================================================

struct Data {
    int value;
};

Data global;
Data* arr[3];

int main() {
    // Setup: all sources are non-null
    arr[0] = new Data();
    arr[1] = new Data();
    arr[2] = &global;

    // CASE 1: Array element (ArraySubscriptExpr)
    Data* p1 = arr[0];      // XFAIL: arr[0] is non-null but DTT loses it
    p1->value = 10;         // ERROR: p1 nullability unknown

    // CASE 2: Constant index doesn't help
    Data* p2 = arr[1];      // Still loses tracking
    p2->value = 20;         // ERROR: p2 nullability unknown

    // CASE 3: Ternary where BOTH branches are non-null
    // This is a subset of 140, but specifically testing non-null branches
    bool flag = true;
    Data local;
    Data* p3 = flag ? new Data() : &local;  // Both branches non-null!
    p3->value = 30;         // XFAIL: Should pass but DTT doesn't analyze ternary

    // CASE 4: Pointer-to-pointer dereference
    Data* inner = new Data();
    Data** pp = &inner;
    Data* p4 = *pp;         // *pp is non-null (it's inner)
    p4->value = 40;         // XFAIL: DTT doesn't track through **

    return 0;
}
