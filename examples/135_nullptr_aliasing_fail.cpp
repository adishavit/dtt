// Example 135: Pointer aliasing — EXPECTED TO FAIL (not yet implemented)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests whether DTT can track nullability through pointer aliasing,
// where multiple pointer variables refer to the same memory location.
//
// ============================================================================
// WHY THIS IS DIFFICULT
// ============================================================================
// DTT tracks properties by VARIABLE NAME, not by memory location:
//
//     std::map<std::string, SymValue> Env;  // "ptr1._nonnull" -> value
//
// When two variables alias the same pointer:
//     int* p1 = &x;
//     int* p2 = p1;  // p2 aliases p1
//
// DTT creates separate entries: "p1._nonnull" and "p2._nonnull"
// But if p1 is reassigned:
//     p1 = nullptr;  // Updates "p1._nonnull" but NOT "p2._nonnull"
//
// This is a FUNDAMENTAL ARCHITECTURAL LIMITATION. True alias analysis requires:
// 1. Points-to analysis (expensive: O(n²) or worse)
// 2. Tracking memory locations, not just variable names
// 3. Whole-program analysis for escaped pointers
//
// This is why Rust's borrow checker is so complex — it solves this problem
// at the language level with ownership semantics.
//
// ============================================================================
// RATIONALE FOR NOT IMPLEMENTING
// ============================================================================
// From our discussion of pros/cons:
// - Adding alias analysis would require ~1500-2000 lines of complex code
// - Compile-time overhead could increase significantly
// - False positive rate would likely be 10-30% on real codebases
// - DTT's strength is SIMPLE numeric property tracking, not memory analysis
//
// The cost/benefit ratio is unfavorable for a Clang plugin architecture.
// Languages like Rust handle this at the language design level.
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// UPDATE: Simple copy (p2 = p1) now WORKS because DTT copies _nonnull on assignment.
// The REAL aliasing limitation is with INDIRECT updates through pointer-to-pointer.
//
// When we modify *pp (where pp points to p1), DTT doesn't know that p1 changed.
// This is the fundamental alias analysis problem.
//
// ============================================================================

struct Data {
    int value;
};

int main() {
    // CASE 1: Simple copy - this NOW WORKS in DTT
    Data* p1 = new Data();  // p1._nonnull = 1
    Data* p2 = p1;          // p2._nonnull = 1 (copied from p1)
    p2->value = 42;         // WORKS: p2 is tracked as non-null

    // CASE 2: Indirect modification through pointer-to-pointer
    // This is where aliasing breaks down

    Data* ptr = nullptr;    // ptr._nonnull = 0
    Data** pp = &ptr;       // pp points to ptr (not tracked by DTT)

    *pp = new Data();       // This modifies ptr through pp!
                            // DTT does not follow pp: it forgets ptr, whose address escaped

    ptr->value = 10;        // ptr's nullability unknown after *pp assignment  dtt-error {{potential null pointer dereference}}

    return 0;
}
