// 728_reference_binding_xfail.cpp
// Gap: DTT doesn't track that writing through reference initializes variable
// These patterns should be safe but DTT reports false positives

// =============================================================================
// Basic reference patterns - DTT should recognize but doesn't yet
// =============================================================================

// Variant 1: Basic reference write should initialize
int variant1_basic_ref() {
    int x;
    int& r = x;
    r = 42;
    return x;  // FALSE POSITIVE: x was initialized via r
}

// Variant 2: Multiple references
int variant2_multi_ref() {
    int x;
    int& r1 = x;
    int& r2 = x;
    r1 = 100;
    return r2;  // FALSE POSITIVE: r2 refers to initialized x
}

// Variant 3: Reference chain
int variant3_ref_chain() {
    int x;
    int& r1 = x;
    int& r2 = r1;
    r2 = 50;
    return x;  // FALSE POSITIVE: x initialized via reference chain
}

// =============================================================================
// Struct reference patterns
// =============================================================================

struct Point { int x, y; };

// Variant 4: Reference to struct member
int variant4_member_ref() {
    Point p;
    int& rx = p.x;
    rx = 10;
    return p.x;  // FALSE POSITIVE: p.x initialized via reference
}

// Variant 5: Reference to entire struct
int variant5_struct_ref() {
    Point p;
    Point& r = p;
    r.x = 1;
    r.y = 2;
    return p.x + p.y;  // FALSE POSITIVE: members initialized via struct ref
}

// =============================================================================
// Reference from pointer dereference
// =============================================================================

// Variant 6: Reference binds to dereferenced pointer
int variant6_ref_from_deref() {
    int x;
    int* p = &x;
    int& r = *p;
    r = 42;
    return x;  // FALSE POSITIVE: x initialized via reference to *p
}

// =============================================================================
// Loop patterns
// =============================================================================

// Variant 7: Reference write in loop
int variant7_loop_ref() {
    int x;
    int& r = x;
    for (int i = 0; i < 1; i++) {
        r = i;
    }
    return x;  // FALSE POSITIVE: x initialized in loop via reference
}

int main() {
    return 0;
}
