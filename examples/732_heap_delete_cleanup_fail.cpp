// 732_heap_delete_cleanup_fail.cpp
// DTT detects reading uninitialized heap memory after reallocation
// All patterns below should trigger DTT errors

// =============================================================================
// Reuse after delete patterns
// =============================================================================

// Variant 1: Reallocate same pointer after delete
int variant1_realloc_after_delete() {
    int* p = new int;
    *p = 42;
    delete p;

    p = new int;  // New allocation, should have fresh state
    int x = *p;   // ERROR: DTT may think p is still initialized from before  dtt-error {{uninitialized heap memory}}
    delete p;
    return x;
}

// Variant 2: Different pointer, same location possible
int variant2_same_location() {
    int* p1 = new int;
    *p1 = 10;
    delete p1;

    int* p2 = new int;  // May get same memory location
    int x = *p2;        // ERROR: Should be uninitialized, but old state may persist  dtt-error {{uninitialized}}
    delete p2;
    return x;
}

// Variant 3: Loop with repeated alloc/delete
int variant3_loop_realloc() {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int* p = new int;
        // First iteration: p is uninitialized
        // Subsequent: stale state from previous iterations may persist
        if (i > 0) {
            sum += *p;  // ERROR: should be uninitialized each iteration  dtt-error {{uninitialized}}
        }
        *p = i;
        delete p;
    }
    return sum;
}

// =============================================================================
// Free patterns (C-style)
// =============================================================================

void* malloc(unsigned long);
void free(void*);

// Variant 4: malloc/free cycle
int variant4_malloc_free() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);

    p = (int*)malloc(sizeof(int));
    int x = *p;  // ERROR: should be uninitialized after fresh malloc  dtt-error {{potential null pointer dereference}}
    free(p);
    return x;
}

// Variant 5: Mixed new/delete with reuse
int variant5_mixed_reuse() {
    int* p = new int(10);
    int x = *p;  // OK: initialized
    delete p;

    p = new int;  // Fresh allocation
    int y = *p;   // ERROR: should be uninitialized  dtt-error {{uninitialized}}
    delete p;
    return x + y;
}

// =============================================================================
// State accumulation (memory leak in analysis)
// =============================================================================

// Variant 6: Many allocations may accumulate stale state
void variant6_many_allocs() {
    for (int i = 0; i < 1000; i++) {
        int* p = new int;
        *p = i;
        delete p;
        // Each iteration should clean HeapObjectStates[p's ResourceId]
        // If not cleaned, state accumulates
    }
}

// =============================================================================
// Safe patterns (should work correctly when gap is fixed)
// =============================================================================

// Safe 1: Init after each allocation
int safe1_init_after_alloc() {
    int* p = new int;
    *p = 1;
    delete p;

    p = new int;
    *p = 2;      // Explicit init
    int x = *p;  // OK: just initialized
    delete p;
    return x;
}

int main() {
    return 0;
}
