// Example 406: Buffer pointer one-past-end dereference — should FAIL
// Dereferencing one-past-end is UB

void test_deref_one_past_end() {
    int arr[10];
    int* end = arr + 10;   // OK to create

    *end = 1;              // ERROR: dereference of one-past-end  dtt-error {{out of bounds}}
}

int main() {
    test_deref_one_past_end();
    return 0;
}
