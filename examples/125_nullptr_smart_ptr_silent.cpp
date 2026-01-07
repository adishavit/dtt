// Example 125: Smart pointers — SILENT LIMITATION (not tracked by DTT)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests whether DTT can track nullability of C++ smart pointers:
// std::unique_ptr, std::shared_ptr, std::optional, etc.
//
// ============================================================================
// WHY THIS IS DIFFICULT
// ============================================================================
// Smart pointers are CLASS TYPES, not raw pointer types.
// They have different semantics:
//
//     std::unique_ptr<Data> ptr;      // Default-constructed, is null
//     std::unique_ptr<Data> ptr(new Data());  // Non-null
//     if (ptr) { ... }                // Uses operator bool()
//     ptr->value = 42;                // Uses operator->()
//     *ptr = value;                   // Uses operator*()
//
// The challenges:
//
// 1. TYPE DETECTION: Smart pointers are template instantiations
//    - std::unique_ptr<Data> vs Data* have different types
//    - Need to recognize std::unique_ptr, std::shared_ptr, etc.
//
// 2. OPERATOR OVERLOADING: operator->() and operator*() are method calls
//    - DTT's VisitMemberExpr/VisitUnaryOperator check isPointerType()
//    - Smart pointers are NOT pointer types, they're class types
//    - Need to intercept CXXOperatorCallExpr instead
//
// 3. LIBRARY INTERNALS: Smart pointer internals vary by implementation
//    - libstdc++: _M_t._M_head_impl
//    - libc++: __ptr_
//    - MSVC STL: _Mypair._Myval2
//    - Can't reliably track internal state
//
// 4. MOVE SEMANTICS: unique_ptr transfers ownership on move
//    - std::unique_ptr<Data> p2 = std::move(p1);
//    - p1 is now null, p2 is non-null
//    - Need to track move operations specially
//
// ============================================================================
// RATIONALE FOR NOT IMPLEMENTING
// ============================================================================
// From our discussion of "Option C: Smart Pointer Focus":
// - Estimated 500-700 lines of code
// - Medium risk due to stdlib coupling
// - Different implementations across compilers
//
// Smart pointer support is a good FUTURE EXTENSION but requires:
// - Hooking into CXXOperatorCallExpr for -> and *
// - Special-casing std::unique_ptr, std::shared_ptr constructors
// - Tracking std::move() for ownership transfer
// - Possibly synthetic annotations for std:: smart pointer methods
//
// ============================================================================
// POSSIBLE FUTURE APPROACH
// ============================================================================
// Register synthetic annotations similar to std::array bounds:
//
//     MethodAnnotations["unique_ptr::operator->/0"] = { requires: [_nonnull != 0] };
//     MethodAnnotations["unique_ptr::operator*/0"] = { requires: [_nonnull != 0] };
//     MethodAnnotations["unique_ptr::operator bool/0"] = { returns: "_nonnull" };
//
// Then track _nonnull through construction and move operations.
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT ignores smart pointers entirely — they're not raw pointers.
// No errors, no tracking. They're invisible to nullptr analysis.
//
// ============================================================================

#include <memory>

struct Data {
    int value;
};

int main() {
    // Smart pointer construction
    std::unique_ptr<Data> ptr1;  // Null
    std::unique_ptr<Data> ptr2(new Data());  // Non-null

    // Bool conversion check
    if (ptr2) {
        // ptr2 is non-null here, but DTT doesn't know
        ptr2->value = 42;  // XFAIL: DTT doesn't track unique_ptr
    }

    // Null smart pointer dereference
    ptr1->value = 10;  // XFAIL: Should error (ptr1 is null) but DTT ignores

    // Move semantics
    std::unique_ptr<Data> ptr3 = std::move(ptr2);
    // ptr2 is now null, ptr3 is non-null
    ptr2->value = 20;  // XFAIL: Should error (ptr2 was moved-from)
    ptr3->value = 30;  // XFAIL: Should pass (ptr3 owns the data)

    return 0;
}
