// 715_uninit_struct_arg_fail.cpp
// DTT detects passing uninitialized struct members (CWE-457)

// =============================================================================
// PART A: Passing uninitialized struct MEMBER to function
// =============================================================================

struct Point { int x; int y; };

void sink_int(int v);

// Variant 1: Pass uninitialized member directly
void variant1_member_direct() {
    Point p;         // p.x and p.y are indeterminate
    sink_int(p.x);   // ERROR: reading p.x is UB  dtt-error {{uninitialized variable 'p.x'}}
}

// Variant 2: Pass uninitialized member, other member initialized
void variant2_partial_init() {
    Point p;
    p.y = 10;        // Only y initialized
    sink_int(p.x);   // ERROR: p.x still uninitialized  dtt-error {{uninitialized variable 'p.x'}}
}

// Variant 3: Nested struct member
struct Line { Point start; Point end; };

void variant3_nested_member() {
    Line l;          // All members indeterminate
    sink_int(l.start.x);  // ERROR: nested member uninitialized  dtt-error {{uninitialized variable 'l.start.x'}}
}

// =============================================================================
// PART B: Passing whole uninitialized struct by value
// =============================================================================

void sink_point(Point p);

// Variant 4: Pass whole uninitialized POD struct
void variant4_whole_struct() {
    Point p;         // Indeterminate
    sink_point(p);   // ERROR: copying reads all members  dtt-error {{uninitialized variable 'p.x'}} dtt-error {{uninitialized variable 'p.y'}}
}

// Variant 5: Partially initialized struct passed by value
void variant5_partial_struct() {
    Point p;
    p.x = 1;         // Only x initialized
    sink_point(p);   // ERROR: copying reads p.y (uninit)  dtt-error {{uninitialized variable 'p.y'}}
}

// =============================================================================
// Safe patterns (should NOT error)
// =============================================================================

// Safe 1: Fully initialized struct
void safe1_fully_init() {
    Point p;
    p.x = 1;
    p.y = 2;
    sink_point(p);   // OK: all members initialized
}

// Safe 2: Brace-initialized struct
void safe2_brace_init() {
    Point p{};       // Zero-initialized
    sink_point(p);   // OK
}

// Safe 3: Designated initializer
void safe3_designated() {
    Point p{.x = 1, .y = 2};
    sink_point(p);   // OK
}

// Safe 4: Aggregate initialization
void safe4_aggregate() {
    Point p = {1, 2};
    sink_point(p);   // OK
}

// Safe 5: Out-parameter pattern (address taken)
void fill_point(Point* out);
void safe5_out_param() {
    Point p;
    fill_point(&p);  // OK: taking address, not reading
    sink_point(p);   // OK: filled by fill_point
}

// Safe 6: Pass address of uninitialized (out-param pattern)
void safe6_pass_address() {
    Point p;
    fill_point(&p);  // OK: not reading p
}

// Safe 7: Only read initialized member
void safe7_read_init_member() {
    Point p;
    p.x = 42;
    sink_int(p.x);   // OK: p.x is initialized
}

int main() {
    return 0;
}
