// 818_std_deque_pass.cpp
// Tests: Safe std::deque usage that should compile without DTT errors

#include <deque>

// =============================================================================
// SECTION 1: Guard with empty() before access
// =============================================================================

// Variant 1.1: if (!empty()) guard then front()
void test_empty_guard_front() {
    std::deque<int> d = {1, 2, 3};
    if (!d.empty()) {
        d.front();  // OK: guarded by !empty()
    }
}

// Variant 1.2: if (!empty()) guard then back()
void test_empty_guard_back() {
    std::deque<int> d = {1, 2, 3};
    if (!d.empty()) {
        d.back();  // OK: guarded
    }
}

// Variant 1.3: negated empty guard
void test_negated_empty() {
    std::deque<int> d;
    d.push_back(42);
    if (!d.empty()) {
        d.front();  // OK: guarded by !empty()
    }
}

// =============================================================================
// SECTION 2: Guard with size() before index access
// =============================================================================

// Variant 2.1: if (index < size()) guard
void test_size_guard(size_t index) {
    std::deque<int> d = {1, 2, 3, 4, 5};
    if (index < d.size()) {
        d[index];  // OK: guarded
    }
}

// Variant 2.2: for loop with i < size()
void test_for_loop() {
    std::deque<int> d = {1, 2, 3};
    for (size_t i = 0; i < d.size(); ++i) {
        d[i] = static_cast<int>(i);  // OK: bounded
    }
}

// Variant 2.3: at() with try-catch (safe by design)
void test_at_safe() {
    std::deque<int> d = {1, 2, 3};
    try {
        d.at(0);  // OK: at() throws on out-of-bounds
    } catch (...) {}
}

// =============================================================================
// SECTION 3: Access after push operations
// =============================================================================

// Variant 3.1: push_back then back()
void test_push_back() {
    std::deque<int> d;
    d.push_back(42);
    d.back();  // OK: just pushed, size >= 1
}

// Variant 3.2: push_front then front()
void test_push_front() {
    std::deque<int> d;
    d.push_front(42);
    d.front();  // OK: just pushed, size >= 1
}

// Variant 3.3: emplace_back then back()
void test_emplace_back() {
    std::deque<int> d;
    d.emplace_back(42);
    d.back();  // OK: just emplaced
}

// =============================================================================
// SECTION 4: Initializer list construction
// =============================================================================

// Variant 4.1: non-empty initializer list
void test_init_list() {
    std::deque<int> d = {1, 2, 3};
    d.front();  // OK: initialized with 3 elements
    d.back();   // OK
    d[0];       // OK
}

// Variant 4.2: access after assign
void test_assign() {
    std::deque<int> d;
    d.assign({1, 2, 3, 4, 5});
    d[4];  // OK: assigned 5 elements
}

// =============================================================================
// SECTION 5: Range-based for
// =============================================================================

// Variant 5.1: range-for iteration
void test_range_for() {
    std::deque<int> d = {1, 2, 3};
    for (auto& elem : d) {
        elem *= 2;  // OK: range-for is safe
    }
}

// Variant 5.2: const range-for
int test_const_range_for() {
    std::deque<int> d = {1, 2, 3};
    int sum = 0;
    for (const auto& elem : d) {
        sum += elem;
    }
    return sum;
}

// =============================================================================
// SECTION 6: Iterator-based access with guards
// =============================================================================

// Variant 6.1: begin != end check
void test_iterator_guard() {
    std::deque<int> d = {1, 2, 3};
    auto it = d.begin();
    if (it != d.end()) {
        *it;  // OK: guarded
    }
}

// =============================================================================
// SECTION 7: Resize and access
// =============================================================================

// Variant 7.1: resize then access
void test_resize() {
    std::deque<int> d;
    d.resize(10);
    d[9] = 42;  // OK: resized to 10 elements
}

// Variant 7.2: resize with value
void test_resize_value() {
    std::deque<int> d;
    d.resize(5, 100);
    d.front();  // OK: size is 5
}

// =============================================================================
// SECTION 8: Insert and access
// =============================================================================

// Variant 8.1: multiple push then access
void test_multiple_push() {
    std::deque<int> d;
    d.push_back(1);
    d.push_front(0);  // Now d = {0, 1}
    d.front();  // OK: size >= 2
    d.back();   // OK
}

// =============================================================================
// SECTION 9: Copy/move preserves size
// =============================================================================

// Variant 9.1: copy from non-empty
void test_copy() {
    std::deque<int> d1 = {1, 2, 3};
    std::deque<int> d2 = d1;
    d2.front();  // OK: copied from non-empty
}

// Variant 9.2: move from non-empty, access destination
void test_move_dest() {
    std::deque<int> d1 = {1, 2, 3};
    std::deque<int> d2 = std::move(d1);
    d2.front();  // OK: moved to d2
}

// =============================================================================
// SECTION 10: Clear then rebuild
// =============================================================================

// Variant 10.1: clear, push, access
void test_clear_rebuild() {
    std::deque<int> d = {1, 2, 3};
    d.clear();
    d.push_back(42);
    d.front();  // OK: rebuilt after clear
}

int main() {
    return 0;
}
