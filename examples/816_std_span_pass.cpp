// 816_std_span_pass.cpp
// Tests: Safe std::span usage that should compile without DTT errors
// Requires C++20
//
// NOTE: DTT currently doesn't track std::span sizes (shows _size = ?).
// This file only includes patterns that work without size tracking:
// - Static extent spans with literal indices
// - Range-based for loops (don't use operator[])

#include <span>
#include <array>
#include <cstddef>

// =============================================================================
// SECTION 1: Static extent span - constant index access
// =============================================================================

// Variant 1.1: access with literal index in bounds
void test_static_literal_index() {
    int arr[10] = {};
    std::span<int, 10> s(arr);
    s[0] = 1;  // OK: 0 < 10
    s[9] = 9;  // OK: 9 < 10
}

// Variant 1.2: computed static index
void test_static_computed_index() {
    int arr[10] = {};
    std::span<int, 10> s(arr);
    constexpr size_t idx = 5;
    s[idx] = 42;  // OK: 5 < 10
}

// =============================================================================
// SECTION 2: Range-based for (no operator[] needed)
// =============================================================================

// Variant 2.1: range-for on static span
void test_range_for_static() {
    int arr[5] = {};
    std::span<int, 5> s(arr);
    for (auto& elem : s) {
        elem = 42;  // OK: range-for is always safe
    }
}

// Variant 2.2: range-for on dynamic span from local array
void test_range_for_dynamic() {
    int arr[10] = {};
    std::span<int> s(arr);
    for (auto& elem : s) {
        elem = 1;  // OK: range-for doesn't use operator[]
    }
}

// Variant 2.3: const range-for
int test_range_for_const() {
    int arr[5] = {1, 2, 3, 4, 5};
    std::span<const int, 5> s(arr);
    int sum = 0;
    for (const auto& elem : s) {
        sum += elem;  // OK
    }
    return sum;
}

// =============================================================================
// SECTION 3: Static extent subspan
// =============================================================================

// Variant 3.1: first<N>() subspan
void test_first_static() {
    int arr[10] = {};
    std::span<int, 10> s(arr);
    auto sub = s.first<5>();  // span<int, 5>
    sub[4] = 42;  // OK: 4 < 5
}

// Variant 3.2: last<N>() subspan
void test_last_static() {
    int arr[10] = {};
    std::span<int, 10> s(arr);
    auto sub = s.last<3>();  // span<int, 3>
    sub[0] = 1;  // OK: 0 < 3
    sub[2] = 3;  // OK: 2 < 3
}

// Variant 3.3: subspan<Offset, Count>()
void test_subspan_static() {
    int arr[10] = {};
    std::span<int, 10> s(arr);
    auto sub = s.subspan<2, 5>();  // span<int, 5>
    sub[4] = 42;  // OK: 4 < 5
}

// =============================================================================
// SECTION 4: data() pointer with guard
// =============================================================================

// Variant 4.1: data() on static extent with guard
void test_data_static() {
    int arr[5] = {};
    std::span<int, 5> s(arr);
    int* p = s.data();
    if (p) {
        p[4] = 42;  // OK: guarded, 4 < 5
    }
}

// =============================================================================
// SECTION 5: size_bytes() query (no access)
// =============================================================================

// Variant 5.1: size_bytes for byte-level info
size_t test_size_bytes() {
    int arr[10] = {};
    std::span<int, 10> s(arr);
    return s.size_bytes();  // OK: just querying
}

// =============================================================================
// SECTION 6: Span from std::array (static extent)
// =============================================================================

// Variant 6.1: span from array
void test_span_from_array() {
    std::array<int, 5> arr = {1, 2, 3, 4, 5};
    std::span<int, 5> s(arr);
    s[4] = 10;  // OK: 4 < 5
}

// =============================================================================
// SECTION 7: extent comparison (compile-time)
// =============================================================================

// Variant 7.1: static extent check
void test_extent_check() {
    int arr[10] = {};
    std::span<int, 10> s(arr);
    static_assert(s.extent == 10);
    s[s.extent - 1] = 42;  // OK: extent - 1 = 9 < 10
}

// =============================================================================
// SECTION 8: as_bytes / as_writable_bytes with range-for
// =============================================================================

// Variant 8.1: as_bytes with range-for
void test_as_bytes() {
    int arr[5] = {1, 2, 3, 4, 5};
    std::span<int, 5> s(arr);
    auto bytes = std::as_bytes(s);
    for (auto b : bytes) {
        (void)b;  // OK: range-for
    }
}

// Variant 8.2: as_writable_bytes with range-for
void test_as_writable_bytes() {
    int arr[5] = {};
    std::span<int, 5> s(arr);
    auto bytes = std::as_writable_bytes(s);
    for (auto& b : bytes) {
        b = std::byte{0};  // OK: range-for
    }
}

int main() {
    return 0;
}
