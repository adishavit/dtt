// 709_uninitialized_pointer_xfail.cpp
// Gap: DTT doesn't track uninitialized pointer state
// These variants compile cleanly with DTT but contain UB
// DTT incorrectly accepts them because it treats null-guards as sufficient
// but comparing/using an uninitialized pointer is UB regardless of the value

// Variant 1: Guarded uninitialized pointer - DTT thinks guard is sufficient
void variant1_guarded_deref() {
    int* p;              // Uninitialized - indeterminate value
    if (p != nullptr) {  // UB: comparing uninitialized pointer is UB!
        *p = 42;         // DTT accepts (guard satisfied) but still UB
    }
}

// Variant 2: Boolean conversion of uninitialized pointer
void variant2_bool_conversion() {
    int* p;
    if (p) {             // UB: boolean conversion of uninitialized pointer
        *p = 42;         // DTT accepts but still UB
    }
}

// Variant 3: Negated check - early return
void variant3_negated_check() {
    int* p;
    if (!p) {            // UB: comparing uninitialized pointer
        return;
    }
    *p = 42;             // DTT accepts (thinks p is proven non-null) but UB
}

// Variant 4: Uninitialized pointer copy then guard
void variant4_copy_then_guard() {
    int* p;
    int* q = p;          // Copies indeterminate value
    if (q != nullptr) {  // UB: comparing
        *q = 42;         // DTT accepts but UB
    }
}

// Variant 5: Uninitialized used in assert-like check
void variant5_assert_check() {
    int* p;
    if (p == nullptr) {  // UB: comparing uninitialized
        return;          // "Early exit if null"
    }
    *p = 42;             // DTT thinks p is proven non-null, but it's uninitialized
}

// Variant 6: Uninitialized void* cast then guard
void variant6_void_cast() {
    void* p;
    int* ip = static_cast<int*>(p);
    if (ip) {            // UB: comparing cast of uninitialized
        *ip = 42;
    }
}

// Variant 7: Uninitialized in loop with guard
void variant7_loop_guard() {
    int* p;
    for (int i = 0; i < 10; i++) {
        if (p) {         // UB: comparing uninitialized (every iteration)
            *p = i;
        }
    }
}

// Variant 8: Double guard
void variant8_double_guard() {
    int* p;
    if (p) {             // UB: first comparison
        if (p != nullptr) {  // UB: second comparison (redundant but still UB)
            *p = 42;
        }
    }
}

// Variant 9: Guard with else branch
void variant9_guard_else() {
    int* p;
    if (p) {             // UB: comparing uninitialized
        *p = 42;
    } else {
        // Do nothing - but the comparison was still UB
    }
}

// Variant 10: Guard in while condition
void variant10_while_guard() {
    int* p;
    while (p) {          // UB: comparing uninitialized
        *p = 42;
        break;
    }
}

int main() {
    return 0;
}
