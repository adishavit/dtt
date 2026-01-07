// Example 124: Pointer-to-container aliasing — SILENT LIMITATION
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests whether DTT correctly tracks nullability when member pointers
// are modified through a pointer to the containing struct.
//
// NOTE: Simple member pointer tracking WORKS in DTT:
//   c.ptr = new Data(); c.ptr->value = 42;  // OK - DTT tracks this
//   c1 = c2;  // OK - DTT copies properties on struct assignment
//   modify(c);  // OK - DTT invalidates on non-const reference
//
// The REAL limitation is ALIASING through pointer-to-container.
//
// ============================================================================
// WHY THIS IS DIFFICULT
// ============================================================================
// When you have a pointer to a container:
//
//     Container c;
//     c.ptr = new Data();   // c.ptr._nonnull = 1
//     Container* pc = &c;   // pc points to c
//     pc->ptr = nullptr;    // Modifies c.ptr through pc!
//
// DTT tracks:
// - "c.ptr._nonnull" = 1 (from c.ptr = new Data())
// - "pc" as non-null (from &c)
// - Assignment to "pc->ptr" — but this is a MemberExpr on pc, not c
//
// The problem: DTT sees pc->ptr = nullptr and tracks "pc->ptr._nonnull" = 0
// But it doesn't know that pc->ptr IS c.ptr. The alias relationship is lost.
//
// ============================================================================
// ARCHITECTURAL CHALLENGE
// ============================================================================
// This is the same aliasing problem as Example 121, but for member pointers.
//
// To handle this, DTT would need to:
// 1. Track that pc = &c creates an alias relationship
// 2. When pc->member is modified, also update c.member
// 3. This requires full points-to analysis
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT tracks c.ptr._nonnull = 1, then separately tracks pc->ptr._nonnull = 0.
// When we dereference c.ptr, DTT still thinks it's non-null (stale).
// This is a false negative — a real null dereference goes undetected.
//
// ============================================================================

struct Data {
    int value;
};

struct Container {
    Data* ptr;
};

int main() {
    Container c;
    c.ptr = new Data();   // c.ptr._nonnull = 1

    Container* pc = &c;   // pc points to c
    pc->ptr = nullptr;    // This modifies c.ptr through pc!
                          // DTT tracks "pc->ptr._nonnull = 0" but not "c.ptr._nonnull = 0"

    c.ptr->value = 42;    // XFAIL: DTT thinks c.ptr is still non-null
                          // Real bug: null pointer dereference

    return 0;
}
