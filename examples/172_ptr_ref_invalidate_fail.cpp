// Example 172: Reference-to-pointer invalidates tracking — should FAIL
// ============================================================================
// Passing a pointer by REFERENCE (int*& p) allows the function to change
// what the pointer points to. Tracking must be invalidated.
//
// f(int*& p) can do: p = nullptr; or p = other_buffer;
// After the call, we don't know what p points to anymore.

void modify_ptr(int*& p);

void test_ref_to_ptr_invalidates() {
    int x = 42;
    int* p = &x;           // p: non-null

    modify_ptr(p);         // p passed by reference - CAN change p

    *p = 10;               // Should FAIL: p's nullability unknown  dtt-error {{potential null pointer dereference}}
}

int main() {
    test_ref_to_ptr_invalidates();
    return 0;
}
