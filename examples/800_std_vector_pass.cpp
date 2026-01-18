// 800_std_vector_pass.cpp
// Tests: Safe std::vector usage patterns that should compile without DTT errors
// These patterns demonstrate proper guards before accessing vector elements

#include <vector>
#include <cstddef>

// =============================================================================
// SECTION 1: empty() guards
// =============================================================================

// Variant 1.1: !empty() guard before front()
void test_empty_guard_front() {
    std::vector<int> v;
    v.push_back(1);
    if (!v.empty()) {
        v.front();  // OK: !empty() proves _size > 0
    }
}

// Variant 1.2: !empty() guard before back()
void test_empty_guard_back() {
    std::vector<int> v;
    v.push_back(42);
    if (!v.empty()) {
        v.back();  // OK: !empty() proves _size > 0
    }
}

// Variant 1.3: !empty() guard before operator[]
void test_empty_guard_index() {
    std::vector<int> v;
    v.push_back(1);
    if (!v.empty()) {
        v[0];  // OK: !empty() proves _size > 0
    }
}

// Variant 1.4: !empty() guard before pop_back()
void test_empty_guard_pop() {
    std::vector<int> v;
    v.push_back(1);
    if (!v.empty()) {
        v.pop_back();  // OK: !empty() proves _size > 0
    }
}

// =============================================================================
// SECTION 2: size() guards
// =============================================================================

// Variant 2.1: size() > 0 guard
void test_size_gt_zero() {
    std::vector<int> v;
    v.push_back(1);
    if (v.size() > 0) {
        v.front();  // OK: size() > 0 proves _size > 0
        v.back();   // OK
    }
}

// Variant 2.2: size() != 0 guard
void test_size_ne_zero() {
    std::vector<int> v;
    v.push_back(1);
    if (v.size() != 0) {
        v.front();  // OK: size() != 0 proves _size > 0
    }
}

// Variant 2.3: size() >= 1 guard
void test_size_ge_one() {
    std::vector<int> v;
    v.push_back(1);
    if (v.size() >= 1) {
        v[0];  // OK: size() >= 1 proves _size >= 1
    }
}

// Variant 2.4: size() guard for specific index
void test_size_guard_index() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    if (v.size() > 2) {
        v[2];  // OK: size() > 2 proves _size >= 3
    }
}

// Variant 2.5: size() guard with variable
void test_size_guard_variable(size_t idx) {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    if (idx < v.size()) {
        v[idx];  // OK: idx < size() proves idx < _size
    }
}

// =============================================================================
// SECTION 3: push_back tracking
// =============================================================================

// Variant 3.1: Single push_back enables access
void test_push_back_single() {
    std::vector<int> v;
    v.push_back(1);  // _size = 1
    v.front();       // OK: push_back proved _size > 0
    v.back();        // OK
    v[0];            // OK
}

// Variant 3.2: Multiple push_backs
void test_push_back_multiple() {
    std::vector<int> v;
    v.push_back(1);  // _size = 1
    v.push_back(2);  // _size = 2
    v.push_back(3);  // _size = 3
    v[0];  // OK: _size >= 1
    v[1];  // OK: _size >= 2
    v[2];  // OK: _size >= 3
}

// Variant 3.3: push_back in loop
void test_push_back_loop() {
    std::vector<int> v;
    for (int i = 0; i < 10; ++i) {
        v.push_back(i);
    }
    // After loop: _size >= 10 (but we can't prove exact value easily)
    if (v.size() >= 5) {
        v[4];  // OK: guarded
    }
}

// Variant 3.4: emplace_back tracking
void test_emplace_back() {
    std::vector<int> v;
    v.emplace_back(42);  // _size = 1
    v.front();           // OK
}

// =============================================================================
// SECTION 4: clear() and rebuild
// =============================================================================

// Variant 4.1: clear() then push_back
void test_clear_push() {
    std::vector<int> v;
    v.push_back(1);
    v.clear();       // _size = 0
    v.push_back(2);  // _size = 1
    v.front();       // OK: push_back after clear
}

// Variant 4.2: clear() then guard
void test_clear_guard() {
    std::vector<int> v;
    v.push_back(1);
    v.clear();
    v.push_back(2);
    if (!v.empty()) {
        v.front();  // OK: guarded
    }
}

// =============================================================================
// SECTION 5: resize() tracking
// =============================================================================

// Variant 5.1: resize() to known size
void test_resize_known() {
    std::vector<int> v;
    v.resize(10);  // _size = 10
    v[0];   // OK
    v[5];   // OK
    v[9];   // OK
}

// Variant 5.2: resize() then guard
void test_resize_guard(size_t n) {
    std::vector<int> v;
    v.resize(n);  // _size = n
    if (n > 0) {
        v[0];  // OK: n > 0 proves _size > 0
    }
}

// =============================================================================
// SECTION 6: for loop patterns
// =============================================================================

// Variant 6.1: Classic index-based for loop
void test_for_index() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    for (size_t i = 0; i < v.size(); ++i) {
        v[i];  // OK: i < size() proves i < _size
    }
}

// Variant 6.2: Range-based for (implicit iterator safety)
void test_for_range() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    for (int x : v) {
        (void)x;  // OK: range-based for is always safe
    }
}

// Variant 6.3: Iterator-based loop
void test_for_iterator() {
    std::vector<int> v;
    v.push_back(1);
    for (auto it = v.begin(); it != v.end(); ++it) {
        *it;  // OK: iterator within bounds
    }
}

// =============================================================================
// SECTION 7: at() (throws, always safe from DTT perspective)
// =============================================================================

// Variant 7.1: at() is bounds-checked at runtime
void test_at() {
    std::vector<int> v;
    v.push_back(1);
    v.at(0);  // OK: at() throws on out-of-bounds, not UB
}

// =============================================================================
// SECTION 8: data() access with size guard
// =============================================================================

// Variant 8.1: data() with size guard
void test_data_access() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    if (v.size() >= 2) {
        int* p = v.data();
        p[0];  // OK: size >= 2
        p[1];  // OK
    }
}

// =============================================================================
// SECTION 9: reserve() (doesn't change size, only capacity)
// =============================================================================

// Variant 9.1: reserve() doesn't enable access
void test_reserve_guard() {
    std::vector<int> v;
    v.reserve(100);  // capacity = 100, but _size = 0
    // v[0];  // Would be ERROR - but we guard properly
    if (!v.empty()) {
        v[0];  // OK: guarded (but won't execute)
    }
}

// =============================================================================
// SECTION 10: assign() tracking
// =============================================================================

// Variant 10.1: assign() with count
void test_assign_count() {
    std::vector<int> v;
    v.assign(5, 42);  // _size = 5
    v[0];  // OK
    v[4];  // OK
}

// Variant 10.2: assign() from range (size known)
void test_assign_range() {
    int arr[] = {1, 2, 3};
    std::vector<int> v;
    v.assign(arr, arr + 3);  // _size = 3
    v[0];  // OK
    v[2];  // OK
}

// =============================================================================
// SECTION 11: insert() tracking
// =============================================================================

// Variant 11.1: insert() single element
void test_insert_single() {
    std::vector<int> v;
    v.insert(v.begin(), 42);  // _size = 1
    v.front();  // OK
}

// Variant 11.2: insert() increases size
void test_insert_after_push() {
    std::vector<int> v;
    v.push_back(1);  // _size = 1
    v.insert(v.begin(), 0);  // _size = 2
    v[0];  // OK
    v[1];  // OK
}

// =============================================================================
// SECTION 12: erase() tracking
// =============================================================================

// Variant 12.1: erase() single, then guard
void test_erase_guard() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);  // _size = 2
    v.erase(v.begin());  // _size = 1
    if (!v.empty()) {
        v[0];  // OK: guarded
    }
}

// =============================================================================
// SECTION 13: swap() tracking
// =============================================================================

// Variant 13.1: swap() preserves tracking
void test_swap() {
    std::vector<int> v1, v2;
    v1.push_back(1);
    v2.push_back(2);
    v2.push_back(3);
    v1.swap(v2);
    // Now v1 has _size = 2 from v2
    if (v1.size() >= 2) {
        v1[0];  // OK
        v1[1];  // OK
    }
}

// =============================================================================
// SECTION 14: copy constructor / assignment
// =============================================================================

// Variant 14.1: copy constructor
void test_copy_ctor() {
    std::vector<int> v1;
    v1.push_back(1);
    std::vector<int> v2(v1);
    if (!v2.empty()) {
        v2[0];  // OK: copied from v1
    }
}

// Variant 14.2: copy assignment
void test_copy_assign() {
    std::vector<int> v1, v2;
    v1.push_back(1);
    v2 = v1;
    if (!v2.empty()) {
        v2[0];  // OK
    }
}

// =============================================================================
// SECTION 15: initializer list
// =============================================================================

// Variant 15.1: initializer list constructor
void test_init_list() {
    std::vector<int> v = {1, 2, 3, 4, 5};  // _size = 5
    v[0];  // OK
    v[4];  // OK
}

// Variant 15.2: initializer list with guard
void test_init_list_guard() {
    std::vector<int> v = {1, 2, 3};
    if (v.size() >= 3) {
        v[2];  // OK
    }
}

int main() {
    // All pass tests
    return 0;
}
