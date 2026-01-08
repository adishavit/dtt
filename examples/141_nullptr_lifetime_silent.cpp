// Example 141: Pointer lifetime and dangling pointers — SILENT (false negative)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests whether DTT can track pointer VALIDITY beyond just nullability:
// - Use-after-free (dangling pointers)
// - Use-after-scope (stack pointers that escape)
// - Invalidated iterators/pointers after container modification
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
// Nullptr tracking handles null vs non-null.
// Lifetime tracking handles valid vs dangling.
//
// These are ORTHOGONAL properties:
// - Non-null + Valid = Safe
// - Non-null + Dangling = DANGEROUS (memory corruption)
// - Null + Valid = N/A (null is never "valid")
// - Null + Dangling = Still just null
//
// ============================================================================
// WHY LIFETIME TRACKING IS HARD
// ============================================================================
// Lifetime analysis requires understanding:
//
// 1. ALLOCATION SITES: Where was memory allocated?
//    - Stack (local variables)
//    - Heap (new/malloc)
//    - Static (globals)
//
// 2. DEALLOCATION: When does memory become invalid?
//    - Stack: End of scope
//    - Heap: delete/free call
//    - Invalidation: vector reallocation, etc.
//
// 3. ESCAPE ANALYSIS: Does a pointer outlive its allocation?
//    - Returning pointer to local variable
//    - Storing stack pointer in longer-lived container
//
// This is essentially what Rust's borrow checker does, but Rust has:
// - Explicit lifetime annotations
// - Ownership model built into the type system
// - Compiler-enforced borrowing rules
//
// In C++, none of this is in the type system. We'd need to infer it all.
//
// ============================================================================
// RATIONALE FOR NOT IMPLEMENTING
// ============================================================================
// From our discussion:
// - "No escape analysis" was listed as a fundamental limitation
// - DTT can't track "pointers that escape function scope"
// - Would need "thread-local or global analysis"
//
// Lifetime tracking is a HARDER problem than nullptr tracking.
// It's why tools like AddressSanitizer exist (runtime checking)
// and why Rust's borrow checker is so valuable (compile-time checking).
//
// DTT focuses on VALUE PROPERTIES (_size, _nonnull) not LIFETIME PROPERTIES.
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

Data* getDanglingPointer() {
    Data local;
    return &local;  // WARNING: Returns pointer to stack variable!
}                   // local is destroyed here, pointer becomes dangling

int main() {
    // Use-after-free
    Data* ptr = new Data();
    ptr->value = 42;  // OK
    delete ptr;
    ptr->value = 10;  // SILENT: Should error (use-after-free)
                      // DTT can't track delete operations

    // Use-after-scope
    Data* escaped = getDanglingPointer();  // Dangling!
    escaped->value = 5;  // SILENT: Should error (dangling pointer)
                         // DTT doesn't do escape analysis

    // Iterator invalidation (related problem)
    // std::vector<int> v = {1, 2, 3};
    // int* ptr = v.data();
    // v.push_back(4);  // May reallocate!
    // *ptr = 10;       // Potentially dangling

    return 0;
}
