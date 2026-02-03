// 729_struct_member_alias_pass.cpp
// Struct member aliasing - pointer to struct enables member initialization
// All patterns below are safe

struct Point { int x, y; };
struct Nested { Point p; int z; };

// =============================================================================
// Basic struct pointer patterns
// =============================================================================

// Pattern 1: Pointer to struct, init via arrow
int pattern1_struct_ptr() {
    Point p;
    Point* ptr = &p;
    ptr->x = 1;
    ptr->y = 2;
    return p.x + p.y;  // OK: members initialized via ptr->
}

// Pattern 2: Multiple pointers to same struct
int pattern2_multi_ptr() {
    Point p;
    Point* p1 = &p;
    Point* p2 = &p;
    p1->x = 10;
    p2->y = 20;
    return p.x + p.y;  // OK: members initialized via different pointers
}

// Pattern 3: Pointer copy
int pattern3_ptr_copy() {
    Point p;
    Point* p1 = &p;
    Point* p2 = p1;
    p2->x = 5;
    p2->y = 6;
    return p.x + p.y;  // OK: p1 and p2 both point to p
}

// =============================================================================
// Nested struct patterns
// =============================================================================

// Pattern 4: Pointer to nested struct
int pattern4_nested() {
    Nested n;
    Nested* ptr = &n;
    ptr->p.x = 1;
    ptr->p.y = 2;
    ptr->z = 3;
    return n.p.x + n.p.y + n.z;  // OK: all members initialized
}

// Pattern 5: Pointer to inner struct
int pattern5_inner_ptr() {
    Nested n;
    Point* pp = &n.p;
    pp->x = 10;
    pp->y = 20;
    n.z = 30;
    return n.p.x + n.p.y + n.z;  // OK: inner struct initialized via pointer
}

// =============================================================================
// Function parameter patterns
// =============================================================================

void init_point(Point* p) {
    p->x = 1;
    p->y = 2;
}

// Pattern 6: Struct pointer as output param
int pattern6_output_param() {
    Point p;
    init_point(&p);
    return p.x + p.y;  // OK: initialized by init_point
}

void init_x(int* px) {
    *px = 42;
}

// Pattern 7: Member pointer as output param
int pattern7_member_output() {
    Point p;
    init_x(&p.x);
    p.y = 10;
    return p.x + p.y;  // OK: p.x initialized by init_x, p.y directly
}

// =============================================================================
// Combined patterns
// =============================================================================

// Pattern 8: Mix of direct and pointer access
int pattern8_mixed() {
    Point p;
    Point* ptr = &p;
    ptr->x = 1;
    p.y = 2;  // Direct access
    return p.x + p.y;  // OK: both members initialized
}

// Pattern 9: Struct in array
int pattern9_array() {
    Point arr[3];
    Point* p = &arr[1];
    p->x = 10;
    p->y = 20;
    return arr[1].x + arr[1].y;  // OK: arr[1] initialized via pointer
}

int main() {
    pattern1_struct_ptr();
    pattern4_nested();
    pattern6_output_param();
    return 0;
}
