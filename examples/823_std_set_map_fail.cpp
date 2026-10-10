// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 823_std_set_map_xfail.cpp
// Tests: Unsafe std::set/std::map usage that DTT SHOULD catch but doesn't yet

#include <set>
#include <map>
#include <string>

// =============================================================================
// SECTION 1: std::set - Access on empty
// =============================================================================

// Variant 1.1: dereference begin() on empty set
void test_set_deref_empty() {
    std::set<int> s;
    *s.begin();  // KNOWN GAP (not asserted): _size == 0, begin() == end()
}

// Variant 1.2: access after clear
void test_set_after_clear() {
    std::set<int> s = {1, 2, 3};
    s.clear();
    *s.begin();  // KNOWN GAP (not asserted): _size == 0
}

// =============================================================================
// SECTION 2: std::set - Dereference end()
// =============================================================================

// Variant 2.1: dereference end() directly
void test_set_deref_end() {
    std::set<int> s = {1, 2, 3};
    *s.end();  // KNOWN GAP (not asserted): end() is not dereferenceable
}

// =============================================================================
// SECTION 3: std::set - find() without guard
// =============================================================================

// Variant 3.1: dereference find() without checking
void test_set_find_no_guard() {
    std::set<int> s = {1, 2, 3};
    *s.find(99);  // KNOWN GAP (not asserted): 99 not in set, returns end()
}

// Variant 3.2: dereference find() on parameter key
void test_set_find_param(int key) {
    std::set<int> s = {1, 2, 3};
    *s.find(key);  // KNOWN GAP (not asserted): can't prove key is in set
}

// =============================================================================
// SECTION 4: std::set - Access on parameter set
// =============================================================================

// Variant 4.1: dereference begin() on parameter
void test_set_param_deref(std::set<int>& s) {
    *s.begin();  // KNOWN GAP (not asserted): can't prove _size > 0
}

// =============================================================================
// SECTION 5: std::set - Wrong branch access
// =============================================================================

// Variant 5.1: access in else of !empty()
void test_set_wrong_branch() {
    std::set<int> s;
    if (!s.empty()) {
        // OK here
    } else {
        *s.begin();  // KNOWN GAP (not asserted): set is empty in else branch
    }
}

// Variant 5.2: access when find() == end()
void test_set_find_wrong_branch() {
    std::set<int> s = {1, 2, 3};
    auto it = s.find(99);
    if (it == s.end()) {
        *it;  // KNOWN GAP (not asserted): it is end()
    }
}

// =============================================================================
// SECTION 6: std::map - at() on missing key
// =============================================================================

// Variant 6.1: at() without checking key exists
void test_map_at_missing() {
    std::map<int, std::string> m = {{1, "one"}};
    m.at(99);  // KNOWN GAP (not asserted): key 99 doesn't exist (throws)
}

// Variant 6.2: at() with parameter key
void test_map_at_param(int key) {
    std::map<int, std::string> m = {{1, "one"}, {2, "two"}};
    m.at(key);  // KNOWN GAP (not asserted): can't prove key exists
}

// =============================================================================
// SECTION 7: std::map - Access on empty map
// =============================================================================

// Variant 7.1: dereference begin() on empty map
void test_map_deref_empty() {
    std::map<int, std::string> m;
    m.begin()->second;  // KNOWN GAP (not asserted): _size == 0
}

// Variant 7.2: at() on empty map
void test_map_at_empty() {
    std::map<int, std::string> m;
    m.at(1);  // ERROR: map is empty, key can't exist  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 8: std::map - at() after erase
// =============================================================================

// Variant 8.1: at() after erasing the key
void test_map_at_after_erase() {
    std::map<int, std::string> m = {{1, "one"}, {2, "two"}};
    m.erase(1);
    m.at(1);  // KNOWN GAP (not asserted): key 1 was erased
}

// Variant 8.2: at() after clear
void test_map_at_after_clear() {
    std::map<int, std::string> m = {{1, "one"}};
    m.clear();
    m.at(1);  // ERROR: map is empty  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 9: std::map - find() without guard
// =============================================================================

// Variant 9.1: dereference find() without checking
void test_map_find_no_guard() {
    std::map<int, std::string> m = {{1, "one"}};
    m.find(99)->second;  // KNOWN GAP (not asserted): 99 not in map, returns end()
}

// =============================================================================
// SECTION 10: std::map - Access on parameter map
// =============================================================================

// Variant 10.1: at() on parameter map with unknown key
void test_map_param_at(std::map<int, std::string>& m, int key) {
    m.at(key);  // KNOWN GAP (not asserted): can't prove key exists
}

// Variant 10.2: dereference begin() on parameter
void test_map_param_deref(std::map<int, std::string>& m) {
    m.begin()->second;  // KNOWN GAP (not asserted): can't prove _size > 0
}

// =============================================================================
// SECTION 11: std::map - Wrong guard (count returns 0)
// =============================================================================

// Variant 11.1: at() in else of count() > 0
void test_map_count_wrong_branch() {
    std::map<int, std::string> m = {{1, "one"}};
    if (m.count(99) > 0) {
        // OK here
    } else {
        m.at(99);  // KNOWN GAP (not asserted): key doesn't exist in else branch
    }
}

// =============================================================================
// SECTION 12: Access moved-from container
// =============================================================================

// Variant 12.1: access moved-from set
void test_set_moved_from() {
    std::set<int> s1 = {1, 2, 3};
    std::set<int> s2 = std::move(s1);
    *s1.begin();  // KNOWN GAP (not asserted): s1 is in moved-from state
}

// Variant 12.2: access moved-from map
void test_map_moved_from() {
    std::map<int, int> m1 = {{1, 1}};
    std::map<int, int> m2 = std::move(m1);
    m1.at(1);  // KNOWN GAP (not asserted): m1 is in moved-from state
}

int main() {
    return 0;
}
