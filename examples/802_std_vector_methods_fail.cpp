// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 802_std_vector_methods_fail.cpp
// Tests: std::vector methods that should be caught by DTT
// These are methods that std::vector catches but std::vector doesn't yet

#include <vector>

// =============================================================================
// SECTION 1: erase() on empty vector
// =============================================================================

// Variant 1.1: erase on default-constructed
void test_erase_empty() {
    std::vector<int> v;
    v.erase(v.begin());  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 1.2: erase after clear
void test_erase_after_clear() {
    std::vector<int> v = {1, 2, 3};
    v.clear();
    v.erase(v.begin());  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 2: emplace() - should increment size
// =============================================================================

// Variant 2.1: emplace then out-of-bounds access
void test_emplace_bounds() {
    std::vector<int> v;
    v.emplace(v.begin(), 42);  // _size should be 1
    v[1];  // ERROR: index 1 >= _size (1)  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 3: assign(count, value) - should set size
// =============================================================================

// Variant 3.1: assign then out-of-bounds
void test_assign_bounds() {
    std::vector<int> v = {1, 2, 3, 4, 5};
    v.assign(2, 0);  // _size = 2
    v[3];  // ERROR: index 3 >= _size (2)  // dtt-error {{_size}}
}

// Variant 3.2: assign to empty then access
void test_assign_empty_access() {
    std::vector<int> v;
    v.assign(0, 42);  // _size = 0
    v.front();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 4: insert(pos, count, value) - should add count to size
// =============================================================================

// Variant 4.1: insert count then out-of-bounds
void test_insert_count_bounds() {
    std::vector<int> v;
    v.insert(v.begin(), 3, 42);  // _size = 3
    v[5];  // ERROR: index 5 >= _size (3)  // dtt-error {{_size}}
}

int main() { return 0; }
