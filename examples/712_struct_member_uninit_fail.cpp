// 712_struct_member_uninit_xfail.cpp
// GAP: DTT doesn't check struct member reads for uninitialized scalars
// Reading p.x where x is uninitialized should be caught

struct Point {
    int x, y;
};

struct Nested {
    Point p;
    int z;
};

// =============================================================================
// Direct struct member reads
// =============================================================================

// Variant 1: Simple member read
int variant1_member_read() {
    Point p;
    return p.x;  // GAP: p.x is uninitialized  dtt-error {{uninitialized}}
}

// Variant 2: Read both members
int variant2_both_members() {
    Point p;
    return p.x + p.y;  // GAP: both p.x and p.y uninitialized  dtt-error {{uninitialized}}
}

// Variant 3: Member in expression
int variant3_member_expr() {
    Point p;
    int result = p.x * 2 + 10;  // GAP: p.x is uninitialized  dtt-error {{uninitialized}}
    return result;
}

// Variant 4: Member in condition
int variant4_member_condition() {
    Point p;
    if (p.x > 0) {  // GAP: p.x is uninitialized  dtt-error {{uninitialized}}
        return 1;
    }
    return 0;
}

// =============================================================================
// Nested struct member reads
// =============================================================================

// Variant 5: Nested member read
int variant5_nested_member() {
    Nested n;
    return n.p.x;  // GAP: n.p.x is uninitialized  dtt-error {{uninitialized}}
}

// Variant 6: Mix of nested levels
int variant6_mixed_levels() {
    Nested n;
    return n.z + n.p.y;  // GAP: both n.z and n.p.y uninitialized  dtt-error {{uninitialized}}
}

// =============================================================================
// Partial initialization patterns
// =============================================================================

// Variant 7: One member init, read other
int variant7_partial_init() {
    Point p;
    p.x = 10;
    return p.y;  // GAP: p.y is still uninitialized  dtt-error {{uninitialized}}
}

// Variant 8: Nested partial init
int variant8_nested_partial() {
    Nested n;
    n.z = 5;
    return n.p.x;  // GAP: n.p.x is uninitialized (only n.z was set)  dtt-error {{uninitialized}}
}

// =============================================================================
// Safe patterns (should NOT trigger when gap is fixed)
// =============================================================================

// Safe 1: Fully initialized struct
int safe1_full_init() {
    Point p = {1, 2};
    return p.x;  // OK: aggregate initialized
}

// Safe 2: Value-initialized struct
int safe2_value_init() {
    Point p{};
    return p.x;  // OK: value-initialized to 0
}

// Safe 3: Member assigned before read
int safe3_assign_before_read() {
    Point p;
    p.x = 10;
    p.y = 20;
    return p.x + p.y;  // OK: both assigned
}

int main() {
    return 0;
}
