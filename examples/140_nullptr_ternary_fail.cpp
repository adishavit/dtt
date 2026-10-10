// Example 140: Ternary/conditional expressions — EXPECTED TO FAIL (not yet implemented)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests whether DTT can track nullability through conditional (ternary)
// expressions like: ptr = condition ? nonNullPtr : nullptr;
//
// ============================================================================
// WHY THIS IS DIFFICULT
// ============================================================================
// Ternary expressions create CONDITIONAL nullability:
//
//     Data* ptr = flag ? new Data() : nullptr;
//
// The result is:
// - Non-null if flag is true
// - Null if flag is false
// - UNKNOWN statically (we don't know flag's value)
//
// DTT's SymValue can represent:
// - CONST: known value (0 or 1)
// - UNKNOWN: no information
// - VAR: reference to another variable
// - ADD/SUB/MUL/DIV: arithmetic expressions
//
// But there's no CONDITIONAL variant:
// - "if flag then 1 else 0" can't be represented
//
// To track ternary expressions, DTT would need:
// 1. A new SymValue kind for conditionals
// 2. Path-sensitive analysis (track both branches)
// 3. SMT encoding for conditional constraints
//
// ============================================================================
// ARCHITECTURAL LIMITATION
// ============================================================================
// DTT is PATH-INSENSITIVE by design. It tracks a single Env state.
//
// Path-sensitive analysis would require:
// - Forking Env at each branch point
// - Merging states at join points (phi nodes)
// - Exponential blowup for deeply nested conditions
//
// This is the difference between:
// - Flow-sensitive: Tracks changes over time (DTT does this)
// - Path-sensitive: Tracks different execution paths (DTT doesn't)
//
// Adding path sensitivity is a fundamental architectural change.
//
// ============================================================================
// RATIONALE FOR NOT IMPLEMENTING
// ============================================================================
// From our discussion:
// - Path sensitivity causes "state space explosion"
// - Invalidation logic becomes O(properties²)
// - Compile-time overhead increases significantly
//
// The ternary case is a specific instance of the general path-sensitivity
// problem. Solving it fully would require architectural changes.
//
// ============================================================================
// WORKAROUND
// ============================================================================
// Use explicit if-else and guards:
//
//     Data* ptr;
//     if (flag) {
//         ptr = new Data();  // ptr._nonnull = 1
//     } else {
//         ptr = nullptr;     // ptr._nonnull = 0
//     }
//     if (ptr != nullptr) {
//         ptr->value = 42;   // Guarded, works
//     }
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT treats ternary result as UNKNOWN and requires explicit null guard.
//
// ============================================================================

struct Data {
    int value;
};

bool getFlag();

int main() {
    bool flag = getFlag();

    // Ternary with mixed nullability
    Data* ptr = flag ? new Data() : nullptr;
    // ptr is non-null if flag, null otherwise
    // DTT can't track this — marks as unknown

    // This should require a guard since we don't know flag
    ptr->value = 42;  // XFAIL: DTT reports unknown (correct!)  dtt-error {{potential null pointer dereference}}
                      // But ideally DTT would give better error message

    // Even when one branch is always non-null
    Data d;
    Data* ptr2 = flag ? &d : new Data();  // Both branches are non-null!
    // DTT doesn't analyze ternary branches
    ptr2->value = 10;  // XFAIL: Should pass (both branches non-null)
                       // But DTT doesn't track ternary at all

    return 0;
}
