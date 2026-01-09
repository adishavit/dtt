// Example 141: Pointer lifetime and dangling pointers — SILENT (false negative)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests whether DTT can track pointer VALIDITY beyond just nullability:
// - Use-after-free (dangling pointers)
// - Use-after-scope (stack pointers that escape)
//
// These bugs SHOULD produce DTT errors but currently compile silently.
// This is a FALSE NEGATIVE — real bugs go undetected.
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
// WHY LIFETIME TRACKING IS HARD
// ============================================================================
// Lifetime analysis requires understanding:
//
// 1. ALLOCATION SITES: Where was memory allocated?
// 2. DEALLOCATION: When does memory become invalid?
// 3. ESCAPE ANALYSIS: Does a pointer outlive its allocation?
//
// This is what Rust's borrow checker does with explicit lifetime annotations.
// In C++, none of this is in the type system.
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT doesn't track lifetime at all. Dangling pointers are invisible.
// Code compiles silently — no DTT errors despite real bugs.
// Use AddressSanitizer (-fsanitize=address) for runtime detection.
//
// When DTT is enhanced to detect these, convert to *_fail.cpp tests.
//
// ============================================================================

struct Data {
    int value;
};

int main() {
    // Use-after-free: DTT tracks ptr as non-null, doesn't track delete
    Data* ptr = new Data();  // ptr._nonnull = 1
    ptr->value = 42;         // OK: ptr known non-null
    delete ptr;              // DTT doesn't track delete!
    ptr->value = 10;         // SILENT: ptr still "non-null" to DTT
                             // Real bug: use-after-free

    // Use-after-scope: inline to avoid function return nullability issues
    Data* escaped;
    {
        Data local;
        escaped = &local;    // escaped._nonnull = 1 (address-of)
    }                        // local destroyed here, escaped is dangling
    escaped->value = 5;      // SILENT: escaped still "non-null" to DTT
                             // Real bug: dangling pointer

    return 0;
}
