// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 825_std_unordered_xfail.cpp
// Tests: Unsafe std::unordered_set/unordered_map usage

#include <unordered_set>
#include <unordered_map>
#include <string>

// =============================================================================
// SECTION 1: std::unordered_set - Access on empty
// =============================================================================

// Variant 1.1: dereference begin() on empty
void test_uset_deref_empty() {
    std::unordered_set<int> s;
    *s.begin();  // KNOWN GAP (not asserted): _size == 0
}

// Variant 1.2: access after clear
void test_uset_after_clear() {
    std::unordered_set<int> s = {1, 2, 3};
    s.clear();
    *s.begin();  // KNOWN GAP (not asserted): _size == 0
}

// =============================================================================
// SECTION 2: std::unordered_set - find() without guard
// =============================================================================

// Variant 2.1: dereference find() without checking
void test_uset_find_no_guard() {
    std::unordered_set<int> s = {1, 2, 3};
    *s.find(99);  // KNOWN GAP (not asserted): 99 not in set, returns end()
}

// Variant 2.2: dereference find() on parameter key
void test_uset_find_param(int key) {
    std::unordered_set<int> s = {1, 2, 3};
    *s.find(key);  // KNOWN GAP (not asserted): can't prove key is in set
}

// =============================================================================
// SECTION 3: std::unordered_set - Dereference end()
// =============================================================================

// Variant 3.1: dereference end() directly
void test_uset_deref_end() {
    std::unordered_set<int> s = {1, 2, 3};
    *s.end();  // KNOWN GAP (not asserted): end() is not dereferenceable
}

// =============================================================================
// SECTION 4: std::unordered_set - Wrong branch
// =============================================================================

// Variant 4.1: access in else of !empty()
void test_uset_wrong_branch() {
    std::unordered_set<int> s;
    if (!s.empty()) {
        // OK here
    } else {
        *s.begin();  // KNOWN GAP (not asserted): empty in else branch
    }
}

// =============================================================================
// SECTION 5: std::unordered_set - Access on parameter
// =============================================================================

// Variant 5.1: dereference begin() on parameter
void test_uset_param_deref(std::unordered_set<int>& s) {
    *s.begin();  // KNOWN GAP (not asserted): can't prove _size > 0
}

// =============================================================================
// SECTION 6: std::unordered_map - at() on missing key
// =============================================================================

// Variant 6.1: at() without checking key exists
void test_umap_at_missing() {
    std::unordered_map<int, std::string> m = {{1, "one"}};
    m.at(99);  // KNOWN GAP (not asserted): key 99 doesn't exist (throws)
}

// Variant 6.2: at() with parameter key
void test_umap_at_param(int key) {
    std::unordered_map<int, std::string> m = {{1, "one"}};
    m.at(key);  // KNOWN GAP (not asserted): can't prove key exists
}

// =============================================================================
// SECTION 7: std::unordered_map - Access on empty
// =============================================================================

// Variant 7.1: dereference begin() on empty
void test_umap_deref_empty() {
    std::unordered_map<int, std::string> m;
    m.begin()->second;  // KNOWN GAP (not asserted): _size == 0
}

// Variant 7.2: at() on empty map
void test_umap_at_empty() {
    std::unordered_map<int, std::string> m;
    m.at(1);  // ERROR: map is empty  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 8: std::unordered_map - at() after erase
// =============================================================================

// Variant 8.1: at() after erasing the key
void test_umap_at_after_erase() {
    std::unordered_map<int, std::string> m = {{1, "one"}, {2, "two"}};
    m.erase(1);
    m.at(1);  // KNOWN GAP (not asserted): key 1 was erased
}

// =============================================================================
// SECTION 9: std::unordered_map - find() without guard
// =============================================================================

// Variant 9.1: dereference find() without checking
void test_umap_find_no_guard() {
    std::unordered_map<int, std::string> m = {{1, "one"}};
    m.find(99)->second;  // KNOWN GAP (not asserted): returns end()
}

// =============================================================================
// SECTION 10: std::unordered_map - Access on parameter
// =============================================================================

// Variant 10.1: at() on parameter map
void test_umap_param_at(std::unordered_map<int, std::string>& m, int key) {
    m.at(key);  // KNOWN GAP (not asserted): can't prove key exists
}

// Variant 10.2: dereference begin() on parameter
void test_umap_param_deref(std::unordered_map<int, std::string>& m) {
    m.begin()->second;  // KNOWN GAP (not asserted): can't prove _size > 0
}

// =============================================================================
// SECTION 11: Access moved-from container
// =============================================================================

// Variant 11.1: access moved-from unordered_set
void test_uset_moved_from() {
    std::unordered_set<int> s1 = {1, 2, 3};
    std::unordered_set<int> s2 = std::move(s1);
    *s1.begin();  // KNOWN GAP (not asserted): moved-from state
}

// Variant 11.2: access moved-from unordered_map
void test_umap_moved_from() {
    std::unordered_map<int, int> m1 = {{1, 1}};
    std::unordered_map<int, int> m2 = std::move(m1);
    m1.at(1);  // KNOWN GAP (not asserted): moved-from state
}

// =============================================================================
// SECTION 12: Wrong guard (count returns 0)
// =============================================================================

// Variant 12.1: at() in else of count() > 0
void test_umap_count_wrong_branch() {
    std::unordered_map<int, std::string> m = {{1, "one"}};
    if (m.count(99) > 0) {
        // OK here
    } else {
        m.at(99);  // KNOWN GAP (not asserted): key doesn't exist in else branch
    }
}

int main() {
    return 0;
}
