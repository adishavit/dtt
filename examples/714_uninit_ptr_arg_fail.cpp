// 714_uninit_ptr_arg_fail.cpp
// DTT detects passing uninitialized pointers by value (CWE-457)
// Reading an uninitialized pointer to pass it is UB

// =============================================================================
// Variant 1: Simple pass uninitialized pointer
// =============================================================================
void sink_ptr(int* p);

void variant1_pass_uninit_ptr() {
    int* p;          // Uninitialized pointer
    sink_ptr(p);     // ERROR: reading p is UB - reading p is UB  dtt-error {{uninitialized pointer 'p'}}
}

// =============================================================================
// Variant 2: Pass uninitialized void*
// =============================================================================
void sink_void(void* p);

void variant2_pass_uninit_void() {
    void* p;         // Uninitialized
    sink_void(p);    // ERROR: reading p is UB  dtt-error {{uninitialized pointer 'p'}}
}

// =============================================================================
// Variant 3: Pass uninitialized pointer to multiple params
// =============================================================================
void sink_two_ptrs(int* a, int* b);

void variant3_multiple_uninit_ptrs() {
    int* p;
    int* q;
    sink_two_ptrs(p, q);  // ERROR: reading p is UB for both p and q  dtt-error {{uninitialized pointer 'p'}} dtt-error {{uninitialized pointer 'q'}}
}

// =============================================================================
// Variant 4: Pass uninitialized pointer as second arg (first is valid)
// =============================================================================
void variant4_mixed_args() {
    int x = 42;
    int* valid = &x;
    int* uninit;
    sink_two_ptrs(valid, uninit);  // ERROR: reading p is UB for uninit only  dtt-error {{uninitialized pointer 'uninit'}}
}

// =============================================================================
// Variant 5: Pass uninitialized function pointer
// =============================================================================
typedef void (*callback_t)(int);
void register_callback(callback_t cb);

void variant5_uninit_func_ptr() {
    callback_t cb;   // Uninitialized function pointer
    register_callback(cb);  // ERROR: reading p is UB  dtt-error {{uninitialized pointer 'cb'}}
}

// =============================================================================
// Variant 6: Pass uninitialized pointer to template function
// =============================================================================
template<typename T>
void sink_template(T* p);

void variant6_template_arg() {
    double* p;       // Uninitialized
    sink_template(p);  // ERROR: reading p is UB  dtt-error {{uninitialized pointer 'p'}}
}

// =============================================================================
// Variant 7: Chained - copy uninit then pass (REMOVED - copy already caught)
// =============================================================================
// Note: int* q = p; is already caught by DTT's pointer copy check.
// This variant tested a different code path, so removed to isolate the gap.

// =============================================================================
// Variant 8: Conditional init - path-sensitive (separate gap, not fixed here)
// =============================================================================
// This is a "may be uninitialized" case requiring path-sensitive analysis.
// DTT's flow-insensitive analysis conservatively marks p as initialized
// after the if-block since one path initializes it. This is a known limitation.
// void variant8_conditional_init(bool cond) {
//     int* p;
//     if (cond) { int x; p = &x; }
//     sink_ptr(p);  // Should warn: p may be uninitialized if !cond
// }

// =============================================================================
// Safe patterns (should NOT error)
// =============================================================================

// Safe 1: Initialized to nullptr
void safe1_nullptr_init() {
    int* p = nullptr;
    sink_ptr(p);     // OK: p is initialized (to null)
}

// Safe 2: Initialized to address
void safe2_address_init() {
    int x = 42;
    int* p = &x;
    sink_ptr(p);     // OK: p is initialized
}

// Safe 3: Assigned before use
void safe3_assigned() {
    int* p;
    int x = 42;
    p = &x;
    sink_ptr(p);     // OK: p was assigned
}

// Safe 4: Out-parameter pattern (address-of)
void init_ptr(int** out);
void safe4_out_param() {
    int* p;
    init_ptr(&p);    // OK: taking address, not reading value
    sink_ptr(p);     // OK: p initialized by init_ptr
}

int main() {
    return 0;
}
