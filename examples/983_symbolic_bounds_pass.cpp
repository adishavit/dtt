// 983_symbolic_bounds_pass.cpp
// Tests: Symbolic size with GUARDED access - should PASS
// Guards prove bounds are safe

#include <vector>
#include <deque>
#include <string>
#include <string_view>
#include <array>
#include <span>

// =============================================================================
// SECTION 1: std::vector - guarded access should pass
// =============================================================================

// Variant 1.1: if (n > 0) guard
int test_vector_guarded(int n) {
    std::vector<int> v;
    v.resize(n);
    if (n > 0) {
        return v[0];  // OK: guard proves n > 0, so 0 < n
    }
    return -1;
}

// Variant 1.2: early return guard
int test_vector_early_return(int n) {
    std::vector<int> v;
    v.resize(n);
    if (n <= 0) return -1;
    return v[0];  // OK: early return proves n > 0
}

// Variant 1.3: !empty() guard
int test_vector_not_empty(int n) {
    std::vector<int> v;
    v.resize(n);
    if (!v.empty()) {
        return v[0];  // OK: !empty() proves _size > 0
    }
    return -1;
}

// Variant 1.4: concrete size (baseline)
int test_vector_concrete() {
    std::vector<int> v;
    v.resize(10);
    return v[5];  // OK: 5 < 10 (concrete)
}

// =============================================================================
// SECTION 2: std::deque - guarded access
// =============================================================================

// Variant 2.1: guarded deque access
int test_deque_guarded(int n) {
    std::deque<int> d;
    d.resize(n);
    if (n > 0) {
        return d[0];  // OK: guard proves n > 0
    }
    return -1;
}

// =============================================================================
// SECTION 3: std::string - guarded access
// =============================================================================

// Variant 3.1: guarded string access
char test_string_guarded(size_t n) {
    std::string s;
    s.resize(n);
    if (n > 0) {
        return s[0];  // OK: guard proves n > 0
    }
    return '\0';
}

// =============================================================================
// SECTION 4: std::string_view - guarded access
// =============================================================================

// Variant 4.1: guarded string_view access
char test_string_view_guarded(const char* p, size_t n) {
    std::string_view sv(p, n);
    if (n > 0) {
        return sv[0];  // OK: guard proves n > 0
    }
    return '\0';
}

// =============================================================================
// SECTION 5: std::span - guarded access
// =============================================================================

// Variant 5.1: guarded span access
int test_span_guarded(int* p, size_t n) {
    std::span<int> sp(p, n);
    if (n > 0) {
        return sp[0];  // OK: guard proves n > 0
    }
    return -1;
}

// =============================================================================
// SECTION 6: std::array - guarded symbolic index
// =============================================================================

// Variant 6.1: guarded array access
int test_array_guarded(size_t i) {
    std::array<int, 10> arr = {};
    if (i < 10) {
        return arr[i];  // OK: guard proves i < 10
    }
    return -1;
}

int main() { return 0; }
