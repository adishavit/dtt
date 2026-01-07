// Example 142: Complex guard expressions — EXPECTED TO FAIL (partially implemented)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests various complex guard patterns that may or may not be recognized:
// - Compound conditions: (ptr != nullptr && ptr->valid)
// - Negated conditions: !(ptr == nullptr)
// - Short-circuit evaluation: (ptr && ptr->ready)
// - Function call guards: if (isValid(ptr))
// - Early return patterns: if (!ptr) return;
//
// ============================================================================
// IMPLEMENTATION STATUS
// ============================================================================
// SUPPORTED:
// ✓ if (ptr != nullptr)
// ✓ if (ptr)
// ✓ if (!ptr) in else branch
// ✓ if (ptr != 0)
// ✓ if (ptr != NULL)
//
// NOT SUPPORTED:
// ✗ if (!(ptr == nullptr))  — Double negation
// ✗ if (ptr != nullptr && condition)  — Compound with non-null stuff
// ✗ if (ptr && ptr->valid)  — Short-circuit member access
// ✗ if (isValid(ptr))  — Function call guards
// ✗ if (!ptr) return; ... ptr->x  — Early return pattern
//
// ============================================================================
// WHY COMPLEX GUARDS ARE HARD
// ============================================================================
// 1. COMPOUND CONDITIONS (&&, ||):
//    DTT's extractGuard handles BinaryOperator but returns after first match.
//    For `ptr != nullptr && ptr->ready`:
//    - First part: ptr != nullptr (handled)
//    - Second part: ptr->ready (would need ptr to be proven non-null!)
//    - Chicken-and-egg: need to prove first before checking second
//
// 2. DOUBLE NEGATION:
//    `!(ptr == nullptr)` is logically `ptr != nullptr`
//    But DTT sees:
//    - UnaryOperator(UO_LNot) containing BinaryOperator(BO_EQ)
//    - Current code handles !ptr but not !(complex expression)
//
// 3. SHORT-CIRCUIT EVALUATION:
//    `ptr && ptr->ready` is safe because:
//    - If ptr is null, short-circuit: ptr->ready never evaluated
//    - If ptr is non-null, ptr->ready is safe
//    DTT would need to understand short-circuit semantics.
//
// 4. FUNCTION CALL GUARDS:
//    `if (isValid(ptr))` relies on isValid's semantics.
//    DTT could support this via dtt:ensures_if_true=arg0._nonnull != 0
//    But we'd need to recognize the pattern and apply the postcondition.
//
// 5. EARLY RETURN PATTERN:
//    if (!ptr) return;
//    ptr->x = 1;  // ptr is definitely non-null here
//
//    This requires DATAFLOW ANALYSIS — understanding that after the if,
//    we must have taken the else branch (didn't return).
//    DTT's TraverseIfStmt adds proof only in then/else blocks, not AFTER.
//
// ============================================================================
// RATIONALE FOR PARTIAL IMPLEMENTATION
// ============================================================================
// We implemented the SIMPLE, HIGH-VALUE cases:
// - Direct null checks cover 80%+ of real-world code
// - Simple guards are easy to write and read
// - Complex patterns can often be rewritten simply
//
// Future improvements could add:
// 1. Early return detection (high value, moderate complexity)
// 2. Compound && conditions (moderate value, moderate complexity)
// 3. Short-circuit semantics (lower value, high complexity)
//
// ============================================================================
// WORKAROUNDS
// ============================================================================
// Rewrite complex guards as simple ones:
//
// Instead of: if (ptr && ptr->valid)
// Write:      if (ptr != nullptr) { if (ptr->valid) { ... } }
//
// Instead of: if (!ptr) return; ptr->x = 1;
// Write:      if (ptr != nullptr) { ptr->x = 1; }
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT may not recognize complex guards and report unknown nullability.
//
// ============================================================================

struct Data {
    int value;
    bool ready;
};

bool isValid(Data* ptr);

int main() {
    Data* ptr = nullptr;

    // Double negation — logically equivalent to ptr != nullptr
    if (!(ptr == nullptr)) {
        ptr->value = 1;  // XFAIL: DTT may not recognize double negation
    }

    // Compound condition with short-circuit
    ptr = new Data();
    ptr->ready = true;
    if (ptr && ptr->ready) {
        ptr->value = 2;  // XFAIL: May not track through compound &&
    }

    // Function call guard (would need annotation)
    if (isValid(ptr)) {
        ptr->value = 3;  // XFAIL: isValid() would need ensures_if_true annotation
    }

    // Early return pattern
    ptr = nullptr;
    if (!ptr) return 0;
    ptr->value = 4;  // XFAIL: DTT doesn't track early return implies non-null

    return 0;
}
