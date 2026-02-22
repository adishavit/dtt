// 712: Struct member initialization — safe patterns
// These should NOT trigger uninitialized member warnings.

struct Point { int x, y; };
struct Nested { Point p; int z; };

// Variant 1: aggregate initialization
int variant1_aggregate() {
    Point p = {1, 2};
    return p.x + p.y;  // OK: aggregate initialized
}

// Variant 2: value initialization (zero-init)
int variant2_value_init() {
    Point p{};
    return p.x + p.y;  // OK: zero-initialized
}

// Variant 3: all members assigned before read
int variant3_assign_before_read() {
    Point p;
    p.x = 10;
    p.y = 20;
    return p.x + p.y;  // OK: both assigned
}

// Variant 4: nested struct fully initialized
int variant4_nested_init() {
    Nested n = {{1, 2}, 3};
    return n.p.x + n.p.y + n.z;  // OK: aggregate initialized
}

// Variant 5: member address taken (may be written via pointer)
int variant5_address_taken() {
    Point p;
    int* px = &p.x;
    int* py = &p.y;
    *px = 10;
    *py = 20;
    return p.x + p.y;  // OK: initialized via pointers
}

// Variant 6: struct passed by pointer to function
void init_point(Point* p) {
    p->x = 1;
    p->y = 2;
}

int variant6_output_param() {
    Point p;
    init_point(&p);
    return p.x + p.y;  // OK: initialized by function
}

// Variant 7: struct reference binding
int variant7_reference() {
    Point p;
    Point& r = p;
    r.x = 1;
    r.y = 2;
    return p.x + p.y;  // OK: initialized via reference
}

int main() { return 0; }
