// 820_std_list_pass.cpp
// Tests: Safe std::list usage that should compile without DTT errors

#include <list>

// =============================================================================
// SECTION 1: Guard with empty() before access
// =============================================================================

// Variant 1.1: if (!empty()) guard then front()
void test_empty_guard_front() {
    std::list<int> lst = {1, 2, 3};
    if (!lst.empty()) {
        lst.front();  // OK: guarded by !empty()
    }
}

// Variant 1.2: if (!empty()) guard then back()
void test_empty_guard_back() {
    std::list<int> lst = {1, 2, 3};
    if (!lst.empty()) {
        lst.back();  // OK: guarded
    }
}

// Variant 1.3: negated empty guard
void test_negated_empty() {
    std::list<int> lst;
    lst.push_back(42);
    if (!lst.empty()) {
        lst.front();  // OK: guarded by !empty()
    }
}

// =============================================================================
// SECTION 2: Guard with size() check
// =============================================================================

// Variant 2.1: if (size() > 0) guard
void test_size_guard() {
    std::list<int> lst = {1, 2, 3};
    if (lst.size() > 0) {
        lst.front();  // OK: guarded
    }
}

// Variant 2.2: if (size() >= n) guard
void test_size_ge_guard() {
    std::list<int> lst = {1, 2, 3, 4, 5};
    if (lst.size() >= 3) {
        lst.front();  // OK: at least 3 elements
    }
}

// =============================================================================
// SECTION 3: Access after push operations
// =============================================================================

// Variant 3.1: push_back then back()
void test_push_back() {
    std::list<int> lst;
    lst.push_back(42);
    lst.back();  // OK: just pushed, size >= 1
}

// Variant 3.2: push_front then front()
void test_push_front() {
    std::list<int> lst;
    lst.push_front(42);
    lst.front();  // OK: just pushed, size >= 1
}

// Variant 3.3: emplace_back then back()
void test_emplace_back() {
    std::list<int> lst;
    lst.emplace_back(42);
    lst.back();  // OK: just emplaced
}

// Variant 3.4: emplace_front then front()
void test_emplace_front() {
    std::list<int> lst;
    lst.emplace_front(42);
    lst.front();  // OK
}

// =============================================================================
// SECTION 4: Initializer list construction
// =============================================================================

// Variant 4.1: non-empty initializer list
void test_init_list() {
    std::list<int> lst = {1, 2, 3};
    lst.front();  // OK: initialized with 3 elements
    lst.back();   // OK
}

// Variant 4.2: access after multiple push_back
void test_multiple_elements() {
    std::list<int> lst;
    lst.push_back(1);
    lst.push_back(2);
    lst.push_back(3);
    lst.front();  // OK: size >= 3
    lst.back();   // OK
}

// =============================================================================
// SECTION 5: Range-based for
// =============================================================================

// Variant 5.1: range-for iteration
void test_range_for() {
    std::list<int> lst = {1, 2, 3};
    for (auto& elem : lst) {
        elem *= 2;  // OK: range-for is safe
    }
}

// Variant 5.2: const range-for
int test_const_range_for() {
    std::list<int> lst = {1, 2, 3};
    int sum = 0;
    for (const auto& elem : lst) {
        sum += elem;
    }
    return sum;
}

// =============================================================================
// SECTION 6: Iterator-based access with guards
// =============================================================================

// Variant 6.1: begin != end check
void test_iterator_guard() {
    std::list<int> lst = {1, 2, 3};
    auto it = lst.begin();
    if (it != lst.end()) {
        *it;  // OK: guarded
    }
}

// =============================================================================
// SECTION 7: Resize and access
// =============================================================================

// Variant 7.1: resize then access
void test_resize() {
    std::list<int> lst;
    lst.resize(10);
    lst.front();  // OK: resized to 10 elements
}

// =============================================================================
// SECTION 8: Insert and access
// =============================================================================

// Variant 8.1: multiple push then access
void test_multiple_push() {
    std::list<int> lst;
    lst.push_back(1);
    lst.push_front(0);  // Now lst = {0, 1}
    lst.front();  // OK: size >= 2
    lst.back();   // OK
}

// =============================================================================
// SECTION 9: Copy/move preserves size
// =============================================================================

// Variant 9.1: copy from non-empty
void test_copy() {
    std::list<int> lst1 = {1, 2, 3};
    std::list<int> lst2 = lst1;
    lst2.front();  // OK: copied from non-empty
}

// Variant 9.2: move from non-empty, access destination
void test_move_dest() {
    std::list<int> lst1 = {1, 2, 3};
    std::list<int> lst2 = std::move(lst1);
    lst2.front();  // OK: moved to lst2
}

// =============================================================================
// SECTION 10: Splice preserves elements
// =============================================================================

// Variant 10.1: emplace_back then access
void test_emplace() {
    std::list<int> lst;
    lst.emplace_back(1);
    lst.emplace_front(0);
    lst.front();  // OK: size >= 2
    lst.back();   // OK
}

// =============================================================================
// SECTION 11: Merge preserves elements
// =============================================================================

// Variant 11.1: merge non-empty lists
void test_merge() {
    std::list<int> lst1 = {1, 3, 5};
    std::list<int> lst2 = {2, 4, 6};
    lst1.merge(lst2);
    lst1.front();  // OK: merged, has elements
}

int main() {
    return 0;
}
