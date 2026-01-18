// 819_std_deque_xfail.cpp
// Tests: Unsafe std::deque usage that DTT SHOULD catch but doesn't yet

#include <deque>

// =============================================================================
// SECTION 1: Access on empty deque
// =============================================================================

// Variant 1.1: front() on default-constructed
void test_front_empty() {
    std::deque<int> d;
    d.front();  // ERROR: _size == 0
}

// Variant 1.2: back() on default-constructed
void test_back_empty() {
    std::deque<int> d;
    d.back();  // ERROR: _size == 0
}

// Variant 1.3: operator[] on empty
void test_subscript_empty() {
    std::deque<int> d;
    d[0];  // ERROR: _size == 0
}

// =============================================================================
// SECTION 2: Access after clear()
// =============================================================================

// Variant 2.1: front() after clear()
void test_front_after_clear() {
    std::deque<int> d = {1, 2, 3};
    d.clear();
    d.front();  // ERROR: _size == 0 after clear
}

// Variant 2.2: back() after clear()
void test_back_after_clear() {
    std::deque<int> d = {1, 2, 3};
    d.clear();
    d.back();  // ERROR
}

// Variant 2.3: operator[] after clear()
void test_subscript_after_clear() {
    std::deque<int> d = {1, 2, 3};
    d.clear();
    d[0];  // ERROR
}

// =============================================================================
// SECTION 3: Access after pop operations
// =============================================================================

// Variant 3.1: access after pop_back empties deque
void test_pop_back_empty() {
    std::deque<int> d = {1};
    d.pop_back();  // Now empty
    d.front();  // ERROR: _size == 0
}

// Variant 3.2: access after pop_front empties deque
void test_pop_front_empty() {
    std::deque<int> d = {1};
    d.pop_front();  // Now empty
    d.back();  // ERROR: _size == 0
}

// Variant 3.3: multiple pops then access
void test_multiple_pops() {
    std::deque<int> d = {1, 2, 3};
    d.pop_back();
    d.pop_back();
    d.pop_back();  // Now empty
    d[0];  // ERROR
}

// =============================================================================
// SECTION 4: Out-of-bounds index access
// =============================================================================

// Variant 4.1: index == size
void test_index_equals_size() {
    std::deque<int> d = {1, 2, 3};
    d[3];  // ERROR: index 3 >= size 3
}

// Variant 4.2: index > size
void test_index_greater_size() {
    std::deque<int> d = {1, 2, 3};
    d[100];  // ERROR: index 100 >= size 3
}

// Variant 4.3: unguarded parameter index
void test_param_index(size_t index) {
    std::deque<int> d = {1, 2, 3, 4, 5};
    d[index];  // ERROR: can't prove index < 5
}

// =============================================================================
// SECTION 5: Access in wrong branch
// =============================================================================

// Variant 5.1: access in else of !empty()
void test_wrong_branch() {
    std::deque<int> d;
    if (!d.empty()) {
        // OK here
    } else {
        d.front();  // ERROR: deque is empty in else branch
    }
}

// Variant 5.2: access after negated size check
void test_negated_size() {
    std::deque<int> d = {1, 2, 3};
    size_t index = 5;
    if (index >= d.size()) {
        d[index];  // ERROR: index is out of bounds here
    }
}

// =============================================================================
// SECTION 6: Access after resize to zero
// =============================================================================

// Variant 6.1: resize(0) then access
void test_resize_zero() {
    std::deque<int> d = {1, 2, 3};
    d.resize(0);
    d.front();  // ERROR: _size == 0
}

// =============================================================================
// SECTION 7: Access after erase all
// =============================================================================

// Variant 7.1: erase all elements
void test_erase_all() {
    std::deque<int> d = {1, 2, 3};
    d.erase(d.begin(), d.end());
    d.front();  // ERROR: _size == 0
}

// =============================================================================
// SECTION 8: Access on parameter deque
// =============================================================================

// Variant 8.1: front() on parameter
void test_param_front(std::deque<int>& d) {
    d.front();  // ERROR: can't prove _size > 0
}

// Variant 8.2: operator[] on parameter
void test_param_subscript(std::deque<int>& d) {
    d[0];  // ERROR: can't prove _size > 0
}

// =============================================================================
// SECTION 9: Access moved-from deque
// =============================================================================

// Variant 9.1: front() on moved-from
void test_moved_from() {
    std::deque<int> d1 = {1, 2, 3};
    std::deque<int> d2 = std::move(d1);
    d1.front();  // ERROR: d1 is in moved-from state
}

// =============================================================================
// SECTION 10: Wrong guard variable
// =============================================================================

// Variant 10.1: guard checks wrong deque
void test_wrong_deque_guard(size_t index) {
    std::deque<int> small = {1, 2};
    std::deque<int> large = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    if (index < large.size()) {
        small[index];  // ERROR: guarded against wrong deque
    }
}

int main() {
    return 0;
}
