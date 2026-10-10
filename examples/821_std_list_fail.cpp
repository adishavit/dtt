// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 821_std_list_fail.cpp
// Tests: Unsafe std::list usage that DTT SHOULD catch but doesn't yet

#include <list>

// =============================================================================
// SECTION 1: Access on empty list
// =============================================================================

// Variant 1.1: front() on default-constructed
void test_front_empty() {
    std::list<int> lst;
    lst.front();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 1.2: back() on default-constructed
void test_back_empty() {
    std::list<int> lst;
    lst.back();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 2: Access after clear()
// =============================================================================

// Variant 2.1: front() after clear()
void test_front_after_clear() {
    std::list<int> lst = {1, 2, 3};
    lst.clear();
    lst.front();  // ERROR: _size == 0 after clear  // dtt-error {{_size}}
}

// Variant 2.2: back() after clear()
void test_back_after_clear() {
    std::list<int> lst = {1, 2, 3};
    lst.clear();
    lst.back();  // ERROR  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 3: Access after pop operations
// =============================================================================

// Variant 3.1: access after pop_back empties list
void test_pop_back_empty() {
    std::list<int> lst = {1};
    lst.pop_back();  // Now empty
    lst.front();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 3.2: access after pop_front empties list
void test_pop_front_empty() {
    std::list<int> lst = {1};
    lst.pop_front();  // Now empty
    lst.back();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 4: Access in wrong branch
// =============================================================================

// Variant 4.1: access in else of !empty()
void test_wrong_branch() {
    std::list<int> lst;
    if (!lst.empty()) {
        // OK here
    } else {
        lst.front();  // ERROR: list is empty in else branch  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 5: Access after resize to zero
// =============================================================================

// Variant 5.1: resize(0) then access
void test_resize_zero() {
    std::list<int> lst = {1, 2, 3};
    lst.resize(0);
    lst.front();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 6: Access after erase all
// =============================================================================

// Variant 6.1: erase all elements
void test_erase_all() {
    std::list<int> lst = {1, 2, 3};
    lst.erase(lst.begin(), lst.end());
    lst.front();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 7: Access on parameter list
// =============================================================================

// Variant 7.1: front() on parameter
void test_param_front(std::list<int>& lst) {
    lst.front();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove _size > 0
}

// Variant 7.2: back() on parameter
void test_param_back(std::list<int>& lst) {
    lst.back();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove _size > 0
}

// =============================================================================
// SECTION 8: Access moved-from list
// =============================================================================

// Variant 8.1: front() on moved-from
void test_moved_from() {
    std::list<int> lst1 = {1, 2, 3};
    std::list<int> lst2 = std::move(lst1);
    lst1.front();  // KNOWN GAP (not asserted): lst1 is in moved-from state
}

// =============================================================================
// SECTION 9: Access after remove_if removes all
// =============================================================================

// Variant 9.1: remove_if might empty list
void test_remove_if() {
    std::list<int> lst = {1, 2, 3};
    lst.remove_if([](int) { return true; });  // Removes all
    lst.front();  // KNOWN GAP (not asserted): might be empty
}

// =============================================================================
// SECTION 10: Access after unique on all-same
// =============================================================================

// Variant 10.1: unique might reduce to one
void test_unique() {
    std::list<int> lst = {1, 1, 1};
    lst.unique();  // Now has 1 element
    // This is actually OK, but if we can't track:
    lst.back();  // Potentially KNOWN GAP (not asserted): if tracking is lost
}

int main() {
    return 0;
}
