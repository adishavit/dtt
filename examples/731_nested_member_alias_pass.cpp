// 731_nested_member_alias_pass.cpp
// Nested member aliasing - pointer to struct enables nested member initialization
// Tests fix for nested member path building (ptr->p.x should mark n.p.x initialized)

struct Inner { int x, y; };
struct Outer { Inner p; int z; };

// =============================================================================
// Basic nested member patterns
// =============================================================================

// Pattern 1: Nested member via pointer (ptr->p.x)
int pattern1_nested_member_ptr() {
    Outer n;
    Outer* ptr = &n;
    ptr->p.x = 10;
    ptr->p.y = 20;
    return n.p.x + n.p.y;  // OK: n.p.x and n.p.y initialized via ptr->p.*
}

// Pattern 2: Mixed nested and direct members
int pattern2_mixed_members() {
    Outer n;
    Outer* ptr = &n;
    ptr->p.x = 1;
    ptr->z = 3;
    n.p.y = 2;  // Direct init
    return n.p.x + n.p.y + n.z;  // OK: all initialized
}

// Pattern 3: Deeply nested (if applicable)
struct Level1 { Inner inner; };
struct Level2 { Level1 l1; };

int pattern3_deeper_nesting() {
    Level2 obj;
    Level2* ptr = &obj;
    ptr->l1.inner.x = 100;
    ptr->l1.inner.y = 200;
    return obj.l1.inner.x + obj.l1.inner.y;  // OK: deeply nested members initialized
}

// Pattern 4: Multiple pointers to same object
int pattern4_multi_ptr_nested() {
    Outer n;
    Outer* p1 = &n;
    Outer* p2 = &n;
    p1->p.x = 5;
    p2->p.y = 10;
    return n.p.x + n.p.y;  // OK: both members initialized via different pointers
}

// =============================================================================
// Struct containing union with nested member
// =============================================================================

union Data { int i; float f; };
struct Container { Data d; int tag; };

// Pattern 5: Nested union in struct via pointer
int pattern5_nested_union() {
    Container c;
    Container* ptr = &c;
    ptr->d.i = 42;
    ptr->tag = 0;
    return c.d.i + c.tag;  // OK: union member and tag initialized
}

int main() {
    pattern1_nested_member_ptr();
    pattern3_deeper_nesting();
    return 0;
}
