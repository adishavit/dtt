// 730_union_member_alias_pass.cpp
// Union member aliasing - pointer to union enables member initialization
// All patterns below are safe

union IntFloat { int i; float f; };
union Data { int i; double d; char c; };

// =============================================================================
// Basic union pointer patterns
// =============================================================================

// Pattern 1: Pointer to union, init member
int pattern1_union_ptr() {
    IntFloat u;
    IntFloat* ptr = &u;
    ptr->i = 42;
    return u.i;  // OK: active member i initialized via ptr
}

// Pattern 2: Change active member via pointer
float pattern2_change_active() {
    IntFloat u;
    IntFloat* ptr = &u;
    ptr->i = 100;  // i is active
    ptr->f = 3.14f;  // Now f is active
    return u.f;  // OK: active member f initialized
}

// Pattern 3: Multiple pointers to union
int pattern3_multi_ptr() {
    IntFloat u;
    IntFloat* p1 = &u;
    IntFloat* p2 = &u;
    p1->i = 10;
    return u.i;  // OK: initialized via p1
}

// =============================================================================
// Union in struct patterns
// =============================================================================

struct Container {
    int tag;
    union { int i; float f; } value;
};

// Pattern 4: Pointer to struct containing union
int pattern4_struct_with_union() {
    Container c;
    Container* ptr = &c;
    ptr->tag = 0;
    ptr->value.i = 42;
    return c.value.i;  // OK: union member initialized via struct pointer
}

// =============================================================================
// Function parameter patterns
// =============================================================================

void init_union(IntFloat* u) {
    u->i = 100;
}

// Pattern 5: Union pointer as output param
int pattern5_output_param() {
    IntFloat u;
    init_union(&u);
    return u.i;  // OK: initialized by init_union
}

// =============================================================================
// Member pointer patterns
// =============================================================================

// Pattern 6: Pointer to union member
int pattern6_member_ptr() {
    IntFloat u;
    int* pi = &u.i;
    *pi = 42;
    return u.i;  // OK: active member i initialized via pointer
}

// Pattern 7: Direct init then pointer read
int pattern7_direct_then_ptr() {
    IntFloat u;
    u.i = 10;
    IntFloat* ptr = &u;
    return ptr->i;  // OK: reading initialized member via pointer
}

int main() {
    pattern1_union_ptr();
    pattern4_struct_with_union();
    pattern5_output_param();
    return 0;
}
