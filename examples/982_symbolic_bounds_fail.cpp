// 982_symbolic_bounds_fail.cpp
// Tests: Symbolic size with unguarded access - should FAIL (Category D warning)
// Covers all containers using boundsCheck(): vector, deque, basic_string, array, span, string_view

#include <vector>
#include <deque>
#include <string>
#include <string_view>
#include <array>
#include <span>

// =============================================================================
// SECTION 1: std::vector - symbolic size, unguarded access
// =============================================================================

// Variant 1.1: resize(n), access [0]
int test_vector_resize_n(int n) {
    std::vector<int> v;
    v.resize(n);
    return v[0];  // ERROR: can't prove 0 < n  dtt-error {{0 < n}}
}

// Variant 1.2: resize(argc) from main
int test_vector_argc(int argc) {
    std::vector<int> v;
    v.resize(argc);
    return v[0];  // ERROR: can't prove 0 < argc  dtt-error {{0 < argc}}
}

// Variant 1.3: constructor with symbolic size
int test_vector_ctor(size_t n) {
    std::vector<int> v(n);
    return v[0];  // ERROR: can't prove 0 < n  dtt-error {{0 < n}}
}

// Variant 1.4: multiple accesses
int test_vector_multi(int n) {
    std::vector<int> v;
    v.resize(n);
    v[0] = 1;  // ERROR: can't prove 0 < n  dtt-error {{0 < n}}
    v.resize(n);  // Restore the size for an independent requirement check.
    v[1] = 2;  // ERROR: can't prove 1 < n  dtt-error {{1 < n}}
    v.resize(n);  // Independent lower-bound gap.
    return v[n-1];  // KNOWN GAP (not asserted): the lower bound requires n > 0.
}

// =============================================================================
// SECTION 2: std::deque - symbolic size, unguarded access
// =============================================================================

// Variant 2.1: resize(n), access [0]
int test_deque_resize(int n) {
    std::deque<int> d;
    d.resize(n);
    return d[0];  // ERROR: can't prove 0 < n  dtt-error {{0 < n}}
}

// =============================================================================
// SECTION 3: std::basic_string - symbolic size, unguarded access
// =============================================================================

// Variant 3.1: resize(n), access [0]
char test_string_resize(size_t n) {
    std::string s;
    s.resize(n);
    return s[0];  // ERROR: can't prove 0 < n  dtt-error {{0 < n}}
}

// =============================================================================
// SECTION 4: std::string_view - symbolic size, unguarded access
// =============================================================================

// Variant 4.1: string_view with symbolic length
char test_string_view(const char* p, size_t n) {
    std::string_view sv(p, n);
    return sv[0];  // ERROR: can't prove 0 < n  dtt-error {{0 < n}}
}

// =============================================================================
// SECTION 5: std::span - symbolic size, unguarded access
// =============================================================================

// Variant 5.1: span with symbolic extent
int test_span(int* p, size_t n) {
    std::span<int> sp(p, n);
    return sp[0];  // ERROR: can't prove 0 < n  dtt-error {{0 < n}}
}

// =============================================================================
// SECTION 6: std::array - concrete size but symbolic index
// =============================================================================

// Variant 6.1: array with symbolic index
int test_array_symbolic_index(size_t i) {
    std::array<int, 10> arr = {};
    return arr[i];  // ERROR: can't prove i < 10  dtt-error {{i < 10}}
}

int main() { return 0; }
