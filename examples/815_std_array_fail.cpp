// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 815_std_array_fail.cpp
// Tests: Unsafe std::array usage that DTT SHOULD catch but doesn't yet

#include <array>
#include <cstddef>

// =============================================================================
// SECTION 1: Out-of-bounds constant index
// =============================================================================

// Variant 1.1: index == size (off-by-one)
void test_index_equals_size() {
    std::array<int, 10> arr{};
    arr[10] = 42;  // ERROR: 10 >= 10  // dtt-error {{_size}}
}

// Variant 1.2: index > size
void test_index_greater_size() {
    std::array<int, 5> arr{};
    arr[100] = 42;  // ERROR: 100 >= 5  // dtt-error {{_size}}
}

// Variant 1.3: at() with out-of-bounds (throws, but still bad)
void test_at_oob() {
    std::array<int, 5> arr{};
    arr.at(5);  // ERROR: 5 >= 5 (would throw std::out_of_range)  // dtt-error {{_size}}
}

// Variant 1.4: negative index (wraps to huge positive)
void test_negative_index() {
    std::array<int, 10> arr{};
    int idx = -1;
    arr[static_cast<size_t>(idx)];  // KNOWN GAP (not asserted): huge index after cast
}

// =============================================================================
// SECTION 2: Unguarded symbolic index
// =============================================================================

// Variant 2.1: parameter index without guard
void test_param_index(size_t index) {
    std::array<int, 100> arr{};
    arr[index] = 42;  // ERROR: can't prove index < 100  // dtt-error {{_size}}
}

// Variant 2.2: computed index without guard
void test_computed_index(size_t x, size_t y) {
    std::array<int, 50> arr{};
    arr[x + y] = 42;  // ERROR: can't prove x + y < 50  // dtt-error {{_size}}
}

// Variant 2.3: index from external source
void test_external_index(size_t user_input) {
    std::array<int, 10> arr{};
    arr[user_input];  // ERROR: user_input unguarded  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 3: Wrong guard condition
// =============================================================================

// Variant 3.1: guard checks wrong array
void test_wrong_array_guard(size_t index) {
    std::array<int, 50> small{};
    std::array<int, 100> large{};
    if (index < large.size()) {
        small[index] = 42;  // ERROR: guarded against wrong array  // dtt-error {{_size}}
    }
}

// Variant 3.2: guard off-by-one (<=)
void test_guard_off_by_one(size_t index) {
    std::array<int, 10> arr{};
    if (index <= arr.size()) {  // Wrong: should be <
        arr[index] = 42;  // ERROR: allows index == 10  // dtt-error {{_size}}
    }
}

// Variant 3.3: guard uses wrong variable
void test_wrong_var_guard(size_t index, size_t other) {
    std::array<int, 20> arr{};
    if (other < arr.size()) {  // Wrong: checks 'other'
        arr[index] = 42;  // ERROR: index not guarded  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 4: front()/back() on zero-size array
// =============================================================================

// Variant 4.1: front() on empty
void test_front_empty() {
    std::array<int, 0> arr{};
    arr.front();  // ERROR: size == 0, no front element (UB)  // dtt-error {{_size}}
}

// Variant 4.2: back() on empty
void test_back_empty() {
    std::array<int, 0> arr{};
    arr.back();  // ERROR: size == 0, no back element (UB)  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 5: data() pointer arithmetic overflow
// =============================================================================

// Variant 5.1: data() + offset >= size
void test_data_offset_oob() {
    std::array<int, 10> arr{};
    int* p = arr.data();
    p[10] = 42;  // KNOWN GAP (not asserted): 10 >= 10
}

// Variant 5.2: data() with unguarded loop
void test_data_unguarded_loop(size_t n) {
    std::array<int, 20> arr{};
    int* p = arr.data();
    for (size_t i = 0; i < n; ++i) {
        p[i] = 0;  // KNOWN GAP (not asserted): n could exceed 20
    }
}

// =============================================================================
// SECTION 6: Loop bounds error
// =============================================================================

// Variant 6.1: loop uses wrong bound
void test_loop_wrong_bound() {
    std::array<int, 5> arr{};
    for (size_t i = 0; i < 10; ++i) {  // Wrong: 10 > 5
        arr[i] = 0;  // ERROR: i can be >= 5  // dtt-error {{_size}}
    }
}

// Variant 6.2: loop with <= instead of <
void test_loop_off_by_one() {
    std::array<int, 10> arr{};
    for (size_t i = 0; i <= arr.size(); ++i) {  // Wrong: <=
        arr[i] = 0;  // KNOWN GAP (not asserted): i can be == 10
    }
}

// Variant 6.3: loop starts at wrong offset
void test_loop_wrong_start() {
    std::array<int, 10> arr{};
    for (size_t i = 5; i < 15; ++i) {  // Wrong: goes to 15
        arr[i] = 0;  // ERROR: i can be >= 10  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 7: 2D array bounds error
// =============================================================================

// Variant 7.1: outer index out of bounds
void test_2d_outer_oob() {
    std::array<std::array<int, 5>, 3> arr{};
    arr[3][0] = 42;  // ERROR: outer index 3 >= 3  // dtt-error {{_size}}
}

// Variant 7.2: inner index out of bounds
void test_2d_inner_oob() {
    std::array<std::array<int, 5>, 3> arr{};
    arr[0][5] = 42;  // KNOWN GAP (not asserted): inner index 5 >= 5
}

// Variant 7.3: both indices out of bounds
void test_2d_both_oob() {
    std::array<std::array<int, 4>, 4> arr{};
    arr[10][10] = 42;  // ERROR: both indices out of bounds  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 8: Access after move (moved-from state)
// =============================================================================

// Variant 8.1: access moved-from array
void test_access_moved_from() {
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b = std::move(a);
    a[0];  // WARN: moved-from state (technically valid but discouraged)
}

// =============================================================================
// SECTION 9: Iterator invalidation (less common for array)
// =============================================================================

// Variant 9.1: iterator past end
void test_iterator_past_end() {
    std::array<int, 5> arr{};
    auto it = arr.end();
    *it;  // KNOWN GAP (not asserted): dereferencing end() iterator
}

// Variant 9.2: advance past end
void test_advance_past_end() {
    std::array<int, 5> arr{};
    auto it = arr.begin();
    std::advance(it, 10);  // Advances past end
    *it;  // KNOWN GAP (not asserted): iterator is past end
}

// =============================================================================
// SECTION 10: Type confusion with size
// =============================================================================

// Variant 10.1: signed/unsigned mismatch in loop
void test_signed_loop() {
    std::array<int, 10> arr{};
    for (int i = 0; i < 10; ++i) {
        arr[i] = i;  // Potentially OK, but signed index
    }
    int neg = -1;
    arr[neg];  // KNOWN GAP (not asserted): negative index (implementation-defined cast)
}

// =============================================================================
// SECTION 11: Access with expression that might overflow
// =============================================================================

// Variant 11.1: multiplication overflow
void test_mult_overflow(size_t x) {
    std::array<int, 100> arr{};
    arr[x * 100] = 42;  // ERROR: x * 100 could overflow or be >= 100  // dtt-error {{_size}}
}

// Variant 11.2: addition overflow
void test_add_overflow(size_t a, size_t b) {
    std::array<int, 50> arr{};
    arr[a + b] = 42;  // ERROR: a + b unguarded  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 12: Template with zero size
// =============================================================================

template<size_t N>
void test_template_access() {
    std::array<int, N> arr{};
    arr[0] = 42;  // ERROR if N == 0  // dtt-error {{_size}}
}

// Variant 12.1: instantiate with zero
void test_zero_instantiation() {
    test_template_access<0>();  // Instantiated subscript is asserted in the template body above.
}

// =============================================================================
// SECTION 13: Comparison doesn't prove bounds in else
// =============================================================================

// Variant 13.1: else branch after size check
void test_else_branch(size_t index) {
    std::array<int, 10> arr{};
    if (index >= arr.size()) {
        // index is out of bounds here
        arr[index] = 42;  // ERROR: accessing out of bounds  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 14: Aliased size doesn't track
// =============================================================================

// Variant 14.1: size stored in variable, array replaced
void test_aliased_size() {
    std::array<int, 10> arr{};
    size_t sz = arr.size();  // sz = 10
    std::array<int, 5> small{};
    // Now using sz with smaller array
    for (size_t i = 0; i < sz; ++i) {
        small[i] = 0;  // ERROR: sz = 10 but small.size() = 5  // dtt-error {{_size}}
    }
}

int main() {
    return 0;
}
