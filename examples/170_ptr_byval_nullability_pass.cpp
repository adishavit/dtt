// Example 170: Pointer passed by value preserves nullability tracking — should PASS
// ============================================================================
// Passing a pointer by value (int* p) cannot change the local pointer variable.
// The function receives a COPY - it cannot make our 'p' become null or change
// what it points to. Nullability tracking should persist after the call.
//
// This is distinct from:
// - f(int*& p)  — reference to pointer, CAN change p
// - f(int** pp) — pointer to pointer, CAN change *pp
//
// The function CAN:
// - Modify *p (the pointed-to value)
// - Delete/free the memory (lifetime issue, not nullability)
// - Store p elsewhere (aliasing)
//
// The function CANNOT:
// - Change the value of local variable p
// - Make p become null
// - Change what address p holds

void unknown_func(int* p);

void test_nullability_persists() {
    int x = 42;
    int* p = &x;           // p: non-null (points to x)

    unknown_func(p);       // p passed by value - local p unchanged

    *p = 10;               // Should PASS: p is still non-null
}

int main() {
    test_nullability_persists();
    return 0;
}
