// Test: Compound preconditions with && and || in .dtt annotation files
// Verifies that logical operators parse correctly and constraints are enforced.

// Stub type matching RangeBuffer annotation from test_logical.dtt
class RangeBuffer {
public:
    RangeBuffer() {}
    void safe_get(int idx);
    void check_or(int val);
    int size();
    void push_back(int val);
};

// Variant 1: && satisfied - non-empty and in-bounds
void test_and_satisfied() {
    RangeBuffer buf;
    buf.push_back(42);      // _size becomes 1
    buf.safe_get(0);         // OK: _size > 0 && 0 < _size
}

// Variant 2: || first branch satisfied
void test_or_first_branch() {
    RangeBuffer buf;
    buf.push_back(1);        // _size becomes 1
    buf.check_or(999);       // OK: _size > 0 (first branch of ||)
}

// Variant 3: || second branch satisfied
void test_or_second_branch() {
    RangeBuffer buf;          // _size = 0
    buf.check_or(0);          // OK: arg0 == 0 (second branch of ||)
}

// Variant 4: || both branches satisfied
void test_or_both_satisfied() {
    RangeBuffer buf;
    buf.push_back(1);        // _size = 1
    buf.check_or(0);          // OK: both _size > 0 and arg0 == 0
}
