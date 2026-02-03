// 729_struct_member_alias_xfail.cpp
// Gap: DTT doesn't track struct member initialization via pointer
// These patterns should be safe but DTT reports false positives

struct Point { int x, y; };
struct Nested { Point p; int z; };

// =============================================================================
// Basic struct pointer - DTT should recognize but doesn't yet
// =============================================================================

// Variant 1: Basic struct pointer init
int variant1_struct_ptr() {
    Point p;
    Point* ptr = &p;
    ptr->x = 1;
    ptr->y = 2;
    return p.x + p.y;  // FALSE POSITIVE: members initialized via ptr->
}

// Variant 2: Pointer copy propagation
int variant2_ptr_copy() {
    Point p;
    Point* p1 = &p;
    Point* p2 = p1;
    p2->x = 5;
    p2->y = 6;
    return p.x + p.y;  // FALSE POSITIVE: should track through copy
}

// =============================================================================
// Nested struct patterns
// =============================================================================

// Variant 3: Nested struct via pointer
int variant3_nested() {
    Nested n;
    Nested* ptr = &n;
    ptr->p.x = 1;
    ptr->p.y = 2;
    ptr->z = 3;
    return n.p.x + n.p.y + n.z;  // FALSE POSITIVE: all initialized via ptr
}

// Variant 4: Pointer to inner struct
int variant4_inner_ptr() {
    Nested n;
    Point* pp = &n.p;
    pp->x = 10;
    pp->y = 20;
    return n.p.x + n.p.y;  // FALSE POSITIVE: inner struct via pointer
}

// =============================================================================
// Member pointer patterns
// =============================================================================

// Variant 5: Pointer to struct member
int variant5_member_ptr() {
    Point p;
    int* px = &p.x;
    int* py = &p.y;
    *px = 1;
    *py = 2;
    return p.x + p.y;  // FALSE POSITIVE: members via member pointers
}

// Variant 6: Mixed pointer and direct
int variant6_mixed() {
    Point p;
    Point* ptr = &p;
    ptr->x = 1;
    // p.y not initialized
    return p.x;  // FALSE POSITIVE for p.x: was initialized via ptr
}

// =============================================================================
// Array of structs
// =============================================================================

// Variant 7: Pointer into struct array
int variant7_array() {
    Point arr[3];
    Point* p = &arr[1];
    p->x = 10;
    p->y = 20;
    return arr[1].x + arr[1].y;  // FALSE POSITIVE: initialized via pointer
}

int main() {
    return 0;
}
