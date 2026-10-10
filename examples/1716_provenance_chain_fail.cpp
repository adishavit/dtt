// Test: Provenance chain diagnostics — full state history on error
// DTT should emit note: diagnostics tracing the complete history of a
// tracked field when reporting an error. This tells the user exactly
// how the tracked property evolved to reach the failing state.
//
// Expected output for each variant: the error PLUS note(s) showing
// state changes that led to the violation.

#include <vector>

// Variant 1: clear after push_back — _size goes 0 → 1 → 0
// Expected notes:
//   note: v._size set to 0 (default constructor)
//   note: v._size set to 1 (push_back)
//   note: v._size set to 0 (clear)
void variant1_clear_then_access() {
    std::vector<int> v;       // _size = 0
    v.push_back(1);           // _size = 1
    v.clear();                // _size = 0
    int x = v.front();        // ERROR: _size > 0 not satisfied  dtt-error {{_size > 0}}
}

// Variant 2: pop_back to empty
// Expected notes:
//   note: v._size set to 1 (constructor with count)
//   note: v._size set to 0 (pop_back)
void variant2_pop_to_empty() {
    std::vector<int> v(1);    // _size = 1
    v.pop_back();             // _size = 0
    int x = v.front();        // ERROR: _size > 0 not satisfied  dtt-error {{_size > 0}}
}

// Variant 3: resize to 0
void variant3_resize_to_zero() {
    std::vector<int> v(5);    // _size = 5
    v.resize(0);              // _size = 0
    int x = v[0];             // ERROR: 0 < _size not satisfied  dtt-error {{out of bounds}}
}
