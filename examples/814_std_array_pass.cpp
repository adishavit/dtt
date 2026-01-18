// 814_std_array_pass.cpp
// Tests: Safe std::array usage that should compile without DTT errors

#include <array>
#include <cstddef>

// =============================================================================
// SECTION 1: Access with constant index (known at compile time)
// =============================================================================

// Variant 1.1: access with literal index in bounds
void test_literal_index() {
    std::array<int, 10> arr{};
    arr[0] = 1;   // OK: 0 < 10
    arr[5] = 5;   // OK: 5 < 10
    arr[9] = 9;   // OK: 9 < 10
}

// Variant 1.2: at() with literal index in bounds
void test_at_literal() {
    std::array<int, 5> arr{};
    arr.at(0);  // OK: 0 < 5
    arr.at(4);  // OK: 4 < 5
}

// Variant 1.3: front() and back() (always valid for non-zero size)
void test_front_back() {
    std::array<int, 3> arr = {1, 2, 3};
    arr.front();  // OK: size > 0
    arr.back();   // OK: size > 0
}

// =============================================================================
// SECTION 2: Loop with index < size()
// =============================================================================

// Variant 2.1: for loop with i < arr.size()
void test_for_loop_size() {
    std::array<int, 10> arr{};
    for (size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i);  // OK: i < size() is guaranteed
    }
}

// Variant 2.2: for loop with i < N (template param)
template<size_t N>
void test_for_loop_N() {
    std::array<int, N> arr{};
    for (size_t i = 0; i < N; ++i) {
        arr[i] = 0;  // OK: i < N
    }
}

// Variant 2.3: range-based for
void test_range_for() {
    std::array<int, 5> arr{};
    for (auto& elem : arr) {
        elem = 42;  // OK: range-based for is always safe
    }
}

// =============================================================================
// SECTION 3: Guarded index access
// =============================================================================

// Variant 3.1: if (index < size) guard
void test_guarded_index(size_t index) {
    std::array<int, 100> arr{};
    if (index < arr.size()) {
        arr[index] = 42;  // OK: guarded
    }
}

// Variant 3.2: early return guard
void test_early_return(size_t index) {
    std::array<int, 50> arr{};
    if (index >= arr.size()) return;
    arr[index] = 42;  // OK: guarded by early return
}

// Variant 3.3: if-else guard (ternary doesn't propagate bounds)
int test_if_else_guard(size_t index) {
    std::array<int, 20> arr{};
    if (index < arr.size()) {
        return arr[index];  // OK: guarded by if
    }
    return -1;
}

// =============================================================================
// SECTION 4: data() pointer access
// =============================================================================

// Variant 4.1: data() with size() bound
void test_data_size() {
    std::array<int, 10> arr{};
    int* p = arr.data();
    for (size_t i = 0; i < arr.size(); ++i) {
        p[i] = 0;  // OK: bounded by size()
    }
}

// Variant 4.2: data() immediate access (known size)
void test_data_immediate() {
    std::array<int, 5> arr{};
    arr.data()[0] = 1;  // OK: 0 < 5
    arr.data()[4] = 5;  // OK: 4 < 5
}

// =============================================================================
// SECTION 5: Range-based for and element access
// =============================================================================

// Variant 5.1: range-based for (safe, no iterator tracking needed)
void test_range_for_ref() {
    std::array<int, 5> arr{};
    for (auto& elem : arr) {
        elem = 42;  // OK: range-based for is safe
    }
}

// Variant 5.2: range-based for with const
int test_range_for_const() {
    std::array<int, 5> arr = {1, 2, 3, 4, 5};
    int sum = 0;
    for (const auto& elem : arr) {
        sum += elem;  // OK
    }
    return sum;
}

// Variant 5.3: indexed for loop with reference
void test_indexed_ref() {
    std::array<int, 3> arr{};
    for (size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i + 1);  // OK: bounded by size()
    }
}

// =============================================================================
// SECTION 6: fill() operation
// =============================================================================

// Variant 6.1: fill entire array
void test_fill() {
    std::array<int, 100> arr;
    arr.fill(0);  // OK: fill is always safe
}

// =============================================================================
// SECTION 7: swap() operation
// =============================================================================

// Variant 7.1: swap same-size arrays
void test_swap() {
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b = {5, 4, 3, 2, 1};
    a.swap(b);  // OK
    a[0];  // OK: still size 5
}

// =============================================================================
// SECTION 8: Multidimensional array
// =============================================================================

// Variant 8.1: 2D array access
void test_2d_array() {
    std::array<std::array<int, 5>, 3> arr{};
    arr[0][0] = 1;  // OK: 0 < 3 and 0 < 5
    arr[2][4] = 2;  // OK: 2 < 3 and 4 < 5
}

// Variant 8.2: 2D array loop
void test_2d_loop() {
    std::array<std::array<int, 4>, 3> arr{};
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            arr[i][j] = static_cast<int>(i * 4 + j);  // OK
        }
    }
}

// =============================================================================
// SECTION 9: std::get<> access
// =============================================================================

// Variant 9.1: std::get with valid index
void test_std_get() {
    std::array<int, 3> arr = {1, 2, 3};
    std::get<0>(arr);  // OK: 0 < 3
    std::get<1>(arr);  // OK: 1 < 3
    std::get<2>(arr);  // OK: 2 < 3
}

// =============================================================================
// SECTION 10: Constexpr array
// =============================================================================

// Variant 10.1: constexpr array access
constexpr int test_constexpr() {
    std::array<int, 3> arr = {10, 20, 30};
    return arr[1];  // OK: 1 < 3
}

// Variant 10.2: constexpr size
void test_constexpr_size() {
    constexpr size_t N = 10;
    std::array<int, N> arr{};
    arr[N - 1] = 42;  // OK: N-1 < N
}

// =============================================================================
// SECTION 11: Structured bindings
// =============================================================================

// Variant 11.1: structured bindings
void test_structured_bindings() {
    std::array<int, 3> arr = {1, 2, 3};
    auto [a, b, c] = arr;  // OK: extracts all 3 elements
    (void)a; (void)b; (void)c;
}

// =============================================================================
// SECTION 12: empty() check for zero-size arrays
// =============================================================================

// Variant 12.1: guard with empty()
void test_empty_guard() {
    std::array<int, 5> arr{};
    if (!arr.empty()) {
        arr[0] = 42;  // OK: not empty means size > 0
    }
}

// Variant 12.2: zero-size array (no access)
void test_zero_size() {
    std::array<int, 0> arr{};
    // No access - which is correct for zero-size
    (void)arr.size();  // OK: size() returns 0
}

// =============================================================================
// SECTION 13: Copy and assignment
// =============================================================================

// Variant 13.1: copy construction
void test_copy() {
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b = a;  // copy
    b[0];  // OK: copy preserves size
}

// Variant 13.2: assignment
void test_assign() {
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b{};
    b = a;
    b[4];  // OK: assignment preserves size
}

// =============================================================================
// SECTION 14: max_size() (same as size for array)
// =============================================================================

// Variant 14.1: use max_size() as bound
void test_max_size(size_t i) {
    std::array<int, 10> arr{};
    if (i < arr.max_size()) {
        arr[i] = 42;  // OK: guarded
    }
}

int main() {
    test_for_loop_N<5>();
    return 0;
}
