// Example 171: Pointer passed by value preserves buffer tracking — should PASS
// ============================================================================
// Passing a pointer by value (int* p) cannot change the local pointer variable.
// The function receives a COPY - it cannot change what our 'p' points to or
// its buffer bounds. Buffer tracking should persist after the call.
//
// This is distinct from:
// - f(int*& p)  — reference to pointer, CAN change p
// - f(int** pp) — pointer to pointer, CAN change *pp
//
// The function CAN:
// - Modify p[i] (the pointed-to values)
// - Delete/free the memory (lifetime issue - dangling pointer, not bounds)
// - Store p elsewhere (aliasing)
//
// The function CANNOT:
// - Change the value of local variable p
// - Change what address p holds
// - Change the buffer size that p tracks

void unknown_func(int* p);

void test_buffer_tracking_persists() {
    int arr[10];
    int* p = arr;          // p: buffer(_size=10)

    unknown_func(p);       // p passed by value - local p unchanged

    p[5] = 1;              // Should PASS: p still tracks 10-element buffer
}

int main() {
    test_buffer_tracking_persists();
    return 0;
}
