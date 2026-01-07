// Example 123: Pointer arithmetic — EXPECTED TO FAIL (not yet implemented)
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This tests whether DTT can track nullability through pointer arithmetic
// operations like ptr + offset, ptr++, ptr[i], etc.
//
// ============================================================================
// WHY THIS IS DIFFICULT
// ============================================================================
// Pointer arithmetic creates NEW pointer values that don't have variable names:
//
//     int arr[10];
//     int* ptr = arr;      // ptr = &arr[0], known non-null
//     int* ptr2 = ptr + 5; // ptr2 = &arr[5], but what's its "name"?
//
// The expression `ptr + 5` creates a temporary pointer value.
// DTT tracks by variable name in Env, but `ptr + 5` isn't a variable.
//
// Even if we track ptr2, what about:
//     *(ptr + 5) = 42;  // Dereference of arithmetic expression
//
// Here we're dereferencing an expression, not a named variable.
// DTT's getExprPath() returns "" for complex expressions.
//
// Full pointer arithmetic tracking would require:
// 1. Tracking base pointer + offset pairs
// 2. Bounds checking (is ptr + 5 within arr's bounds?)
// 3. Handling negative offsets, pointer differences
// 4. Expression-level tracking, not just variable-level
//
// ============================================================================
// RATIONALE FOR NOT IMPLEMENTING
// ============================================================================
// From our discussion of implementation complexity:
// - Pointer arithmetic is HARD because it involves both:
//   a) Nullability (is the base pointer null?)
//   b) Bounds (is the offset within valid range?)
//
// - DTT already tracks BOUNDS via _size constraints
// - Combining bounds + nullability is complex
// - The expression tracking infrastructure would need major changes
//
// This is "Option A: Full nullptr tracking" from our discussion —
// estimated 1500-2000 lines, high risk, may not achieve acceptable
// false positive rate.
//
// ============================================================================
// CURRENT BEHAVIOR
// ============================================================================
// DTT doesn't track the arithmetic expression and reports unknown.
// This is conservative but correct — we can't prove safety.
//
// ============================================================================

struct Data {
    int value;
};

int main() {
    Data arr[10];
    Data* ptr = arr;  // ptr points to arr[0], known non-null (array address)

    // Direct dereference of ptr works
    ptr->value = 1;  // This should work (and might, since ptr = arr)

    // But pointer arithmetic creates untracked expressions
    Data* ptr2 = ptr + 5;  // ptr2 points to arr[5]

    // DTT loses track after arithmetic
    ptr2->value = 42;  // XFAIL: DTT may report unknown nullability

    // Even worse: inline arithmetic
    (ptr + 3)->value = 10;  // XFAIL: Expression not tracked at all

    return 0;
}
