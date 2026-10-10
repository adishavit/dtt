// Example 173: Pointer-to-pointer invalidates tracking — should FAIL
// ============================================================================
// Passing a pointer-to-pointer (int** pp) allows the function to change
// what *pp points to. Tracking for the pointed-to pointer must be invalidated.
//
// f(int** pp) can do: *pp = nullptr; or *pp = other_buffer;
// After the call, we don't know what *pp points to anymore.

void modify_via_pp(int** pp);

void test_ptr_to_ptr_invalidates() {
    int x = 42;
    int* p = &x;           // p: non-null

    modify_via_pp(&p);     // &p passed - function CAN change p via *pp

    *p = 10;               // Should FAIL: p's nullability unknown  dtt-error {{potential null pointer dereference}}
}

int main() {
    test_ptr_to_ptr_invalidates();
    return 0;
}
