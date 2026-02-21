// Test: address-of expression (&x) satisfies _nonnull preconditions
//
// When a .dtt annotation requires arg0._nonnull != 0, passing &stack_var
// should be recognized as always non-null (address-of can never be null).
// Uses guarded_deref(1) from test_custom.dtt: requires arg0._nonnull != 0

void guarded_deref(int* p);

// Variant 1: &local passed directly as argument
void variant_addr_of_local() {
    int x = 42;
    guarded_deref(&x);  // &x is always non-null
}

// Variant 2: &struct_member
struct S { int val; };
void variant_addr_of_member() {
    S s;
    s.val = 10;
    guarded_deref(&s.val);  // &s.val is always non-null
}

// Variant 3: &array_element
void variant_addr_of_array_element() {
    int arr[5] = {1, 2, 3, 4, 5};
    guarded_deref(&arr[0]);  // &arr[0] is always non-null
}

// Variant 4: array decay (array name as pointer)
void variant_array_decay() {
    int arr[5] = {1, 2, 3, 4, 5};
    guarded_deref(arr);  // array decays to non-null pointer
}
