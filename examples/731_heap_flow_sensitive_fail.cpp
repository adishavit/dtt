// 731_heap_flow_sensitive_fail.cpp
// DTT detects reading uninitialized heap memory in conditional patterns
// All patterns below should trigger DTT errors

// =============================================================================
// Conditional initialization patterns
// =============================================================================

// Variant 1: Init in one branch, read after merge
int variant1_one_branch_init(bool cond) {
    int* p = new int;
    if (cond) {
        *p = 42;
    }
    int x = *p;  // KNOWN GAP (not asserted): may be uninitialized if !cond
    delete p;
    return x;
}

// Variant 2: Init in both branches (should be OK)
int variant2_both_branches(bool cond) {
    int* p = new int;
    if (cond) {
        *p = 1;
    } else {
        *p = 2;
    }
    int x = *p;  // Should be OK: all paths initialize
    delete p;
    return x;
}

// Variant 3: Nested conditionals
int variant3_nested_cond(bool a, bool b) {
    int* p = new int;
    if (a) {
        if (b) {
            *p = 1;
        }
        // ERROR: if a && !b, p is uninitialized
    }
    // ERROR: if !a, p is uninitialized
    int x = *p;
    delete p;
    return x;
}

// Variant 4: Loop with conditional init
int variant4_loop_cond(int n) {
    int* p = new int;
    for (int i = 0; i < n; i++) {
        if (i == 5) {
            *p = i;
        }
    }
    int x = *p;  // KNOWN GAP (not asserted): uninitialized if n <= 5
    delete p;
    return x;
}

// Variant 5: Early return leaves uninitialized
int variant5_early_return(bool cond) {
    int* p = new int;
    if (cond) {
        delete p;
        return 0;
    }
    // At this point p is still uninitialized
    int x = *p;  // ERROR: p is uninitialized  dtt-error {{uninitialized}}
    delete p;
    return x;
}

// =============================================================================
// Switch statement patterns
// =============================================================================

// Variant 6: Switch with incomplete cases
int variant6_switch_incomplete(int val) {
    int* p = new int;
    switch (val) {
        case 1: *p = 10; break;
        case 2: *p = 20; break;
        // default case missing - p uninitialized
    }
    int x = *p;  // KNOWN GAP (not asserted): uninitialized if val != 1 && val != 2
    delete p;
    return x;
}

// Variant 7: Switch with fallthrough
int variant7_switch_fallthrough(int val) {
    int* p = new int;
    switch (val) {
        case 1:
            // fallthrough without init
        case 2:
            *p = 20;
            break;
        default:
            break;  // no init
    }
    int x = *p;  // KNOWN GAP (not asserted): uninitialized in some cases
    delete p;
    return x;
}

// =============================================================================
// Safe patterns (should NOT trigger when gap is fixed)
// =============================================================================

// Safe 1: Init before branch
int safe1_init_before_branch(bool cond) {
    int* p = new int;
    *p = 0;  // Init before branch
    if (cond) {
        *p = 1;
    }
    int x = *p;  // OK: initialized on all paths
    delete p;
    return x;
}

// Safe 2: All switch cases init
int safe2_switch_complete(int val) {
    int* p = new int;
    switch (val) {
        case 1: *p = 10; break;
        case 2: *p = 20; break;
        default: *p = 0; break;
    }
    int x = *p;  // OK: all cases initialize
    delete p;
    return x;
}

int main() {
    return 0;
}
