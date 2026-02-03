// 730_union_member_alias_xfail.cpp
// Gap: DTT doesn't track union member initialization via pointer
// These patterns should be safe but DTT reports false positives

union IntFloat { int i; float f; };

struct Container {
    int tag;
    union { int i; float f; } value;
};

// =============================================================================
// Basic union pointer - DTT should recognize but doesn't yet
// =============================================================================

// Variant 1: Basic union pointer init
int variant1_union_ptr() {
    IntFloat u;
    IntFloat* ptr = &u;
    ptr->i = 42;
    return u.i;  // FALSE POSITIVE: active member initialized via ptr
}

// Variant 2: Pointer copy
int variant2_ptr_copy() {
    IntFloat u;
    IntFloat* p1 = &u;
    IntFloat* p2 = p1;
    p2->i = 100;
    return u.i;  // FALSE POSITIVE: should track through pointer copy
}

// =============================================================================
// Union in struct patterns
// =============================================================================

// Variant 3: Struct containing union
int variant3_struct_with_union() {
    Container c;
    Container* ptr = &c;
    ptr->tag = 0;
    ptr->value.i = 42;
    return c.value.i;  // FALSE POSITIVE: union in struct via pointer
}

// =============================================================================
// Member pointer patterns
// =============================================================================

// Variant 4: Pointer to union member
int variant4_member_ptr() {
    IntFloat u;
    int* pi = &u.i;
    *pi = 42;
    return u.i;  // FALSE POSITIVE: member initialized via pointer to member
}

// Variant 5: Float member via pointer
float variant5_float_member() {
    IntFloat u;
    IntFloat* ptr = &u;
    ptr->f = 3.14f;
    return u.f;  // FALSE POSITIVE: float member via pointer
}

// =============================================================================
// Active member tracking via pointer
// =============================================================================

// Variant 6: Change active member via pointer
int variant6_change_active() {
    IntFloat u;
    IntFloat* ptr = &u;
    ptr->i = 1;     // i is active
    ptr->f = 2.0f;  // f is now active
    ptr->i = 3;     // i is active again
    return u.i;     // FALSE POSITIVE: should track active member via pointer
}

int main() {
    return 0;
}
