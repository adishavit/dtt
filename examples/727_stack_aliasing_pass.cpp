// 727_stack_aliasing_pass.cpp
// Stack variable aliasing - writing through pointer initializes the variable
// All patterns below are safe - variable is initialized via pointer write

// =============================================================================
// Basic aliasing patterns
// =============================================================================

// Pattern 1: Write through pointer initializes stack variable
int pattern1_basic_alias() {
    int x;
    int* p = &x;
    *p = 42;
    return x;  // OK: x initialized via *p = 42
}

// Pattern 2: Multiple aliases, write through one
int pattern2_multi_alias() {
    int x;
    int* p = &x;
    int* q = p;
    *q = 100;
    return x;  // OK: x initialized via alias chain
}

// Pattern 3: Write then read through different aliases
int pattern3_cross_alias_read() {
    int x;
    int* p = &x;
    int* q = &x;
    *p = 50;
    return *q;  // OK: x initialized, reading via q
}

// Pattern 4: Alias to array element
int pattern4_array_element_alias() {
    int arr[10];
    int* p = &arr[5];
    *p = 42;
    return arr[5];  // OK: arr[5] initialized via pointer
}

// =============================================================================
// Struct member aliasing
// =============================================================================

struct Point { int x, y; };

// Pattern 5: Alias to struct member
int pattern5_struct_member_alias() {
    Point p;
    int* px = &p.x;
    int* py = &p.y;
    *px = 10;
    *py = 20;
    return p.x + p.y;  // OK: both members initialized via pointers
}

// Pattern 6: Alias to entire struct
int pattern6_struct_alias() {
    Point p;
    Point* pp = &p;
    pp->x = 1;
    pp->y = 2;
    return p.x + p.y;  // OK: initialized via struct pointer
}

// =============================================================================
// Conditional aliasing (all paths initialize)
// =============================================================================

// Pattern 7: Write via pointer in both branches
int pattern9_branch_alias(bool cond) {
    int x;
    int* p = &x;
    if (cond) {
        *p = 1;
    } else {
        *p = 2;
    }
    return x;  // OK: initialized on all paths
}

// Pattern 10: Direct init in one branch, pointer in other
int pattern10_mixed_init(bool cond) {
    int x;
    int* p = &x;
    if (cond) {
        x = 1;
    } else {
        *p = 2;
    }
    return x;  // OK: initialized on all paths
}

// =============================================================================
// Reference patterns (references are like pointers)
// =============================================================================

// Pattern 11: Reference alias
int pattern11_ref_alias() {
    int x;
    int& r = x;
    r = 42;
    return x;  // OK: x initialized via reference
}

// Pattern 12: Const reference to initialized
int pattern12_const_ref() {
    int x = 0;
    const int& r = x;
    return r;  // OK: x was initialized
}

int main() {
    pattern1_basic_alias();
    pattern5_struct_member_alias();
    return 0;
}
