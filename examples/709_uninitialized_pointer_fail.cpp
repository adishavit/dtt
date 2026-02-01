// 709_uninitialized_pointer_fail.cpp
// DTT detects use of uninitialized pointers
// All variants below trigger DTT errors because using an uninitialized
// pointer in any way (comparison, dereference, copy) is undefined behavior

// Variant 1: Guarded uninitialized pointer
void variant1_guarded_deref() {
    int* p;              // Uninitialized - indeterminate value
    if (p != nullptr) {  // ERROR: comparing uninitialized pointer is UB
        *p = 42;
    }
}

// Variant 2: Boolean conversion of uninitialized pointer
void variant2_bool_conversion() {
    int* p;
    if (p) {             // ERROR: boolean conversion of uninitialized pointer
        *p = 42;
    }
}

// Variant 3: Negated check - early return
void variant3_negated_check() {
    int* p;
    if (!p) {            // ERROR: comparing uninitialized pointer
        return;
    }
    *p = 42;
}

// Variant 4: Uninitialized pointer copy then guard
void variant4_copy_then_guard() {
    int* p;
    int* q = p;          // ERROR: copying uninitialized pointer
    if (q != nullptr) {
        *q = 42;
    }
}

// Variant 5: Uninitialized used in assert-like check
void variant5_assert_check() {
    int* p;
    if (p == nullptr) {  // ERROR: comparing uninitialized pointer
        return;
    }
    *p = 42;
}

// Variant 6: Uninitialized void* cast then guard
void variant6_void_cast() {
    void* p;
    int* ip = static_cast<int*>(p);  // ERROR: casting uninitialized pointer
    if (ip) {
        *ip = 42;
    }
}

// Variant 7: Uninitialized in loop with guard
void variant7_loop_guard() {
    int* p;
    for (int i = 0; i < 10; i++) {
        if (p) {         // ERROR: comparing uninitialized pointer
            *p = i;
        }
    }
}

// Variant 8: Double guard
void variant8_double_guard() {
    int* p;
    if (p) {             // ERROR: comparing uninitialized pointer
        if (p != nullptr) {
            *p = 42;
        }
    }
}

// Variant 9: Guard with else branch
void variant9_guard_else() {
    int* p;
    if (p) {             // ERROR: comparing uninitialized pointer
        *p = 42;
    } else {
        // Do nothing
    }
}

// Variant 10: Guard in while condition
void variant10_while_guard() {
    int* p;
    while (p) {          // ERROR: comparing uninitialized pointer
        *p = 42;
        break;
    }
}

int main() {
    return 0;
}
