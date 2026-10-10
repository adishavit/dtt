// Example 141: Pointer lifetime and dangling pointers — FAIL (DTT catches UAF)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// DTT tracks pointer VALIDITY beyond just nullability:
// - Use-after-free (dangling pointers)
// - Use-after-scope (stack pointers that escape)
//
// These bugs produce DTT errors at compile time.
//
// ============================================================================
// WHY THIS IS DIFFERENT FROM NULLPTR
// ============================================================================
// Nullptr tracking asks: "Is this pointer null or not?"
// Lifetime tracking asks: "Is this pointer still valid to dereference?"
//
// A pointer can be:
// - Null: Never valid to dereference
// - Valid: Points to live memory, safe to dereference
// - Dangling: Was valid, now points to freed/invalid memory
//
// These are ORTHOGONAL properties:
// - Non-null + Valid = Safe
// - Non-null + Dangling = DANGEROUS (memory corruption)
//
// ============================================================================
// LIFETIME TRACKING
// ============================================================================
// Lifetime analysis requires understanding:
//
// 1. ALLOCATION SITES: Where was memory allocated?
// 2. DEALLOCATION: When does memory become invalid?
// 3. ESCAPE ANALYSIS: Does a pointer outlive its allocation?
//
// DTT tracks allocation and deallocation to detect use-after-free.
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT tracks lifetime for heap allocations (new, malloc, etc.) and detects
// use-after-free when the pointer is dereferenced after deallocation.
//
// ============================================================================

struct Data {
    int value;
};

int main() {
    // Use-after-free: DTT tracks allocation and deallocation
    Data* ptr = new Data();  // ptr._nonnull = 1, lifetime = Valid
    ptr->value = 42;         // OK: ptr known non-null and valid
    delete ptr;              // DTT marks ptr as Freed
    ptr->value = 10;         // ERROR: DTT detects use-after-free  dtt-error {{use-after-free}}

    // Use-after-scope: stack pointers not tracked yet (future enhancement)
    Data* escaped;
    {
        Data local;
        escaped = &local;    // escaped._nonnull = 1 (address-of)
    }                        // local destroyed here, escaped is dangling
    escaped->value = 5;      // Not tracked: stack lifetime not yet implemented

    return 0;
}
