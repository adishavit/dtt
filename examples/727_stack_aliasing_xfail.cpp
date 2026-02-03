// 727_stack_aliasing_xfail.cpp
// Gap: DTT doesn't track that writing through pointer initializes stack variable
// These patterns should be safe but DTT reports false positives

// =============================================================================
// Basic aliasing - DTT should recognize but doesn't yet
// =============================================================================

// Variant 1: Basic pointer write should initialize x
int variant1_basic_alias() {
    int x;
    int* p = &x;
    *p = 42;
    return x;  // FALSE POSITIVE: x was initialized via *p
}

// Variant 2: Multiple levels of aliasing
int variant2_multi_level() {
    int x;
    int* p = &x;
    int* q = p;
    int* r = q;
    *r = 100;
    return x;  // FALSE POSITIVE: x initialized via alias chain
}

// Variant 3: Read through different alias
int variant3_cross_read() {
    int x;
    int* p = &x;
    int* q = &x;
    *p = 42;
    return *q;  // FALSE POSITIVE: x initialized, q points to same location
}

// =============================================================================
// Struct aliasing patterns
// =============================================================================

struct Point { int x, y; };

// Variant 4: Struct member via pointer
int variant4_struct_member() {
    Point p;
    int* px = &p.x;
    *px = 10;
    return p.x;  // FALSE POSITIVE: p.x was initialized via pointer
}

// Variant 5: Struct via struct pointer
int variant5_struct_ptr() {
    Point p;
    Point* pp = &p;
    pp->x = 1;
    pp->y = 2;
    return p.x + p.y;  // FALSE POSITIVE: p initialized via struct pointer
}

// =============================================================================
// Reference aliasing patterns
// =============================================================================

// Variant 6: Reference write should initialize
int variant6_ref_init() {
    int x;
    int& r = x;
    r = 42;
    return x;  // FALSE POSITIVE: x initialized via reference
}

// Variant 7: Reference chain
int variant7_ref_chain() {
    int x;
    int& r1 = x;
    int& r2 = r1;
    r2 = 100;
    return x;  // FALSE POSITIVE: x initialized via reference chain
}

// =============================================================================
// Array element aliasing
// =============================================================================

// Variant 8: Array element via pointer
int variant8_array_element() {
    int arr[10];
    int* p = &arr[5];
    *p = 42;
    return arr[5];  // FALSE POSITIVE: arr[5] initialized via pointer
}

// =============================================================================
// Mixed patterns
// =============================================================================

// Variant 9: Pointer and direct assignment
int variant9_mixed(bool cond) {
    int x;
    int* p = &x;
    if (cond) {
        *p = 1;  // Initialize via pointer
    } else {
        x = 2;   // Initialize directly
    }
    return x;  // FALSE POSITIVE on pointer path
}

// Variant 10: Loop initialization via pointer
int variant10_loop_ptr() {
    int x;
    int* p = &x;
    for (int i = 0; i < 1; i++) {
        *p = i;
    }
    return x;  // FALSE POSITIVE: x initialized in loop via pointer
}

int main() {
    return 0;
}
