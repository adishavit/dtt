// 728_reference_binding_pass.cpp
// Reference binding - writing through reference initializes the variable
// All patterns below are safe

// =============================================================================
// Basic reference patterns
// =============================================================================

// Pattern 1: Write through reference initializes variable
int pattern1_basic_ref() {
    int x;
    int& r = x;
    r = 42;
    return x;  // OK: x initialized via r = 42
}

// Pattern 2: Multiple references to same variable
int pattern2_multi_ref() {
    int x;
    int& r1 = x;
    int& r2 = x;
    r1 = 100;
    return r2;  // OK: x initialized, r2 refers to same x
}

// Pattern 3: Reference chain
int pattern3_ref_chain() {
    int x;
    int& r1 = x;
    int& r2 = r1;  // r2 also refers to x
    r2 = 50;
    return x;  // OK: x initialized via r2
}

// Pattern 4: Reference as function parameter (output)
void init_ref(int& out) {
    out = 42;
}

int pattern4_ref_param() {
    int x;
    init_ref(x);
    return x;  // OK: x initialized by init_ref
}

// Pattern 5: Const reference to initialized value
int pattern5_const_ref() {
    int x = 10;
    const int& r = x;
    return r;  // OK: x was initialized
}

// =============================================================================
// Struct reference patterns
// =============================================================================

struct Point { int x, y; };

// Pattern 6: Reference to struct, init members
int pattern6_struct_ref() {
    Point p;
    Point& r = p;
    r.x = 1;
    r.y = 2;
    return p.x + p.y;  // OK: members initialized via reference
}

// Pattern 7: Reference to struct member
int pattern7_member_ref() {
    Point p;
    int& rx = p.x;
    int& ry = p.y;
    rx = 10;
    ry = 20;
    return p.x + p.y;  // OK: members initialized via references
}

// =============================================================================
// Combined pointer and reference patterns
// =============================================================================

// Pattern 8: Pointer from reference address
int pattern8_ptr_from_ref() {
    int x;
    int& r = x;
    int* p = &r;  // p points to x
    *p = 42;
    return x;  // OK: x initialized via pointer to reference
}

// Pattern 9: Reference from dereferenced pointer
int pattern9_ref_from_ptr() {
    int x;
    int* p = &x;
    int& r = *p;  // r refers to x
    r = 42;
    return x;  // OK: x initialized via reference to dereference
}

// =============================================================================
// Conditional patterns (all paths initialize)
// =============================================================================

// Pattern 10: Both branches initialize via reference
int pattern10_branch_ref(bool cond) {
    int x;
    int& r = x;
    if (cond) {
        r = 1;
    } else {
        r = 2;
    }
    return x;  // OK: initialized on all paths
}

int main() {
    pattern1_basic_ref();
    pattern4_ref_param();
    pattern6_struct_ref();
    return 0;
}
