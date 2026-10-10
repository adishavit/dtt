// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 817_std_span_fail.cpp
// Tests: std::span patterns that DTT SHOULD handle but doesn't yet
// Requires C++20
//
// NOTE: This file includes TWO categories:
// 1. Unsafe code DTT should reject (but doesn't track span sizes)
// 2. Safe guarded code DTT should allow (but doesn't track span sizes)
// Both fail because DTT shows _size = ? for all spans.

#include <span>
#include <array>
#include <vector>
#include <cstddef>

// =============================================================================
// SECTION 0: SAFE patterns DTT should allow but rejects (missing span tracking)
// =============================================================================

// Variant 0.1: guarded dynamic span access - SAFE but DTT rejects
void test_guarded_should_pass(size_t index) {
    int arr[100]{};
    std::span<int> s(arr);
    if (index < s.size()) {
        s[index] = 42;  // SAFE: guarded, but DTT shows _size = ?
    }
}

// Variant 0.2: loop with size() bound - SAFE but DTT rejects
void test_loop_should_pass() {
    int arr[20]{};
    std::span<int> s(arr);
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] = static_cast<int>(i);  // SAFE: bounded, but DTT shows _size = ?
    }
}

// Variant 0.3: span from vector with range-index - SAFE but DTT rejects
void test_vector_span_should_pass() {
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::span<int> s(v);
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] *= 2;  // SAFE: bounded, but DTT shows _size = ?
    }
}

// Variant 0.4: empty() check should enable index 0 - SAFE but DTT rejects
void test_empty_check_should_pass() {
    int arr[10]{};
    std::span<int> s(arr);
    if (!s.empty()) {
        s[0] = 42;  // SAFE: not empty, but DTT shows _size = ?
    }
}

// =============================================================================
// SECTION 1: Out-of-bounds constant index (static extent)
// =============================================================================

// Variant 1.1: index == extent (off-by-one)
void test_static_oob() {
    int arr[10] = {};
    std::span<int, 10> s(arr);
    s[10] = 42;  // ERROR: 10 >= 10  // dtt-error {{_size}}
}

// Variant 1.2: index > extent
void test_static_far_oob() {
    int arr[5] = {};
    std::span<int, 5> s(arr);
    s[100] = 42;  // ERROR: 100 >= 5  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 2: Unguarded dynamic span access
// =============================================================================

// Variant 2.1: parameter index without guard
void test_dynamic_unguarded(std::span<int> s, size_t index) {
    s[index] = 42;  // ERROR: can't prove index < size()  // dtt-error {{_size}}
}

// Variant 2.2: front() on potentially empty span
void test_front_maybe_empty(std::span<int> s) {
    s.front();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove size() > 0
}

// Variant 2.3: back() on potentially empty span
void test_back_maybe_empty(std::span<int> s) {
    s.back();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove size() > 0
}

// Variant 2.4: access at index 0 without empty check
void test_index_zero_no_guard(std::span<int> s) {
    s[0] = 42;  // ERROR: span might be empty  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 3: Empty span access
// =============================================================================

// Variant 3.1: front() on empty span
void test_front_empty() {
    std::span<int> s;  // empty span
    s.front();  // KNOWN GAP (not asserted): size() == 0
}

// Variant 3.2: back() on empty span
void test_back_empty() {
    std::span<int> s;
    s.back();  // KNOWN GAP (not asserted): size() == 0
}

// Variant 3.3: index access on empty span
void test_index_empty() {
    std::span<int> s;
    s[0];  // ERROR: size() == 0  // dtt-error {{_size}}
}

// Variant 3.4: zero-extent static span
void test_zero_extent() {
    std::span<int, 0> s;
    s.front();  // ERROR: extent == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 4: Wrong guard condition
// =============================================================================

// Variant 4.1: guard checks wrong span
void test_wrong_span_guard(std::span<int> small, std::span<int> large, size_t i) {
    if (i < large.size()) {
        small[i] = 42;  // ERROR: guarded against wrong span  // dtt-error {{_size}}
    }
}

// Variant 4.2: guard off-by-one (<=)
void test_guard_off_by_one(std::span<int> s, size_t i) {
    if (i <= s.size()) {  // Wrong: should be <
        s[i] = 42;  // ERROR: allows i == size()  // dtt-error {{_size}}
    }
}

// Variant 4.3: guard uses wrong variable
void test_wrong_var_guard(std::span<int> s, size_t i, size_t j) {
    if (j < s.size()) {  // Wrong: checks j
        s[i] = 42;  // ERROR: i not guarded  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 5: Subspan overflow
// =============================================================================

// Variant 5.1: first(n) where n > size()
void test_first_overflow(std::span<int> s) {
    auto sub = s.first(100);  // KNOWN GAP (not asserted): if size() < 100
    (void)sub;
}

// Variant 5.2: last(n) where n > size()
void test_last_overflow(std::span<int> s) {
    auto sub = s.last(100);  // KNOWN GAP (not asserted): if size() < 100
    (void)sub;
}

// Variant 5.3: subspan(offset) where offset > size()
void test_subspan_offset_overflow(std::span<int> s) {
    auto sub = s.subspan(100);  // KNOWN GAP (not asserted): if size() < 100
    (void)sub;
}

// Variant 5.4: subspan(offset, count) overflow
void test_subspan_count_overflow(std::span<int> s) {
    auto sub = s.subspan(0, 100);  // KNOWN GAP (not asserted): if size() < 100
    (void)sub;
}

// Variant 5.5: subspan offset + count > size
void test_subspan_combined_overflow(std::span<int> s) {
    if (s.size() >= 10) {
        auto sub = s.subspan(8, 5);  // KNOWN GAP (not asserted): 8 + 5 = 13 > 10
        (void)sub;
    }
}

// =============================================================================
// SECTION 6: Static subspan exceeds extent (compile-time check by stdlib)
// =============================================================================

// NOTE: These patterns trigger static_assert in the standard library,
// not DTT errors. Commenting out since they're caught at compile time.
//
// void test_first_static_overflow() {
//     int arr[5] = {};
//     std::span<int, 5> s(arr);
//     auto sub = s.first<10>();  // static_assert fails: 10 > 5
// }
//
// void test_subspan_static_overflow() {
//     int arr[5] = {};
//     std::span<int, 5> s(arr);
//     auto sub = s.subspan<10>();  // static_assert fails: 10 > 5
// }

// =============================================================================
// SECTION 7: data() pointer misuse
// =============================================================================

// Variant 7.1: data() access beyond size
void test_data_oob(std::span<int> s) {
    int* p = s.data();
    p[s.size()] = 42;  // KNOWN GAP (not asserted): index == size
}

// Variant 7.2: data() with unguarded offset
void test_data_unguarded(std::span<int> s, size_t offset) {
    s.data()[offset] = 42;  // KNOWN GAP (not asserted): offset unguarded
}

// =============================================================================
// SECTION 8: Loop bounds error
// =============================================================================

// Variant 8.1: loop uses wrong bound
void test_loop_wrong_bound(std::span<int, 5> s) {
    for (size_t i = 0; i < 10; ++i) {  // Wrong: 10 > 5
        s[i] = 0;  // ERROR: i can be >= 5  // dtt-error {{_size}}
    }
}

// Variant 8.2: loop with <= instead of <
void test_loop_off_by_one(std::span<int> s) {
    for (size_t i = 0; i <= s.size(); ++i) {  // Wrong: <=
        s[i] = 0;  // ERROR: i can == size()  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 9: Span from invalid source
// =============================================================================

// Variant 9.1: span outlives source vector
std::span<int> test_dangling_span() {
    std::vector<int> v = {1, 2, 3};
    return std::span<int>(v);  // KNOWN GAP (not asserted): v destroyed, span dangles
}

// Variant 9.2: span from temporary
void test_span_from_temp() {
    std::span<const int> s(std::vector<int>{1, 2, 3});
    s[0];  // ERROR: temporary destroyed, span dangles  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 10: Iterator misuse
// =============================================================================

// Variant 10.1: dereference end()
void test_deref_end(std::span<int> s) {
    auto it = s.end();
    *it;  // KNOWN GAP (not asserted): dereferencing end() iterator
}

// Variant 10.2: advance past end
void test_advance_past_end(std::span<int, 5> s) {
    auto it = s.begin();
    std::advance(it, 10);  // Past end
    *it;  // KNOWN GAP (not asserted): iterator past end
}

// =============================================================================
// SECTION 11: Access in wrong branch
// =============================================================================

// Variant 11.1: access in else of empty check
void test_wrong_branch(std::span<int> s) {
    if (s.empty()) {
        s[0];  // ERROR: span is empty in this branch  // dtt-error {{_size}}
    }
}

// Variant 11.2: access after negated guard
void test_negated_guard(std::span<int> s, size_t i) {
    if (i >= s.size()) {
        s[i];  // ERROR: i is out of bounds here  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 12: Comparison doesn't prove bounds in else
// =============================================================================

// Variant 12.1: else branch after size check
void test_comparison_else(std::span<int> s, size_t i) {
    if (i < s.size()) {
        // OK here
    } else {
        s[i];  // ERROR: i >= size() in else branch  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 13: Span size changed underneath
// =============================================================================

// Variant 13.1: vector resized after span created
void test_vector_resize() {
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::span<int> s(v);
    v.clear();  // Invalidates span
    s[0];  // ERROR: span now dangles/points to invalid memory  // dtt-error {{_size}}
}

// Variant 13.2: vector reallocated
void test_vector_realloc() {
    std::vector<int> v = {1, 2, 3};
    std::span<int> s(v);
    v.reserve(1000);  // Might reallocate
    s[0];  // ERROR: span might be invalid after reallocation  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 14: Multiple indexing with dependency
// =============================================================================

// Variant 14.1: second index depends on first but unchecked
void test_dependent_index(std::span<int> s, size_t i) {
    if (i < s.size()) {
        s[i + 1];  // ERROR: i+1 might be >= size()  // dtt-error {{_size}}
    }
}

// Variant 14.2: computed index overflow
void test_computed_index(std::span<int> s, size_t a, size_t b) {
    if (a < s.size() && b < s.size()) {
        s[a + b];  // ERROR: a + b might be >= size()  // dtt-error {{_size}}
    }
}

int main() {
    return 0;
}
